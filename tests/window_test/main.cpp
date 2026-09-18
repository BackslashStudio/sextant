#define _USE_MATH_DEFINES
#include <sextant/figure.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <iostream>

// 1. Plot gallery — every basic plot type in one subplot grid.
static void test_axes_gallery() {
    constexpr int N = 200;
    std::vector<double> x(N), y_sin(N), y_cos(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y_sin[i] = std::sin(x[i]);
        y_cos[i] = std::cos(x[i]);
    }

    // A closed five-point path (`loop`), drawn thick so the seam's join is
    // visible.
    constexpr int P = 5;
    std::vector<double> pent_x(P), pent_y(P);
    for (int i = 0; i < P; ++i) {
        const double t = M_PI_2 + i * 2.0 * M_PI / P;
        pent_x[i] = M_PI + 1.3 * std::cos(t);
        pent_y[i] = 0.55 * std::sin(t);
    }

    std::mt19937 rng(42);
    std::normal_distribution<double> dist_a(1.0, 0.4), dist_b(-1.0, 0.4);
    constexpr int NS = 100;
    std::vector<double> xa(NS), ya(NS), xb(NS), yb(NS);
    for (int i = 0; i < NS; ++i) {
        xa[i] = dist_a(rng);
        ya[i] = dist_a(rng);
        xb[i] = dist_b(rng);
        yb[i] = dist_b(rng);
    }

    const std::vector<double> months = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const std::vector<double> sales = {42, 55, 61, 49, 78, 83, 91, 88, 74, 65, 53, 70};
    const std::vector<double> cats = {1, 2, 3, 4, 5, 6};
    const std::vector<double> deltas = {3.2, -1.5, 2.8, -0.9, 1.1, -2.4};

    std::normal_distribution<double> ndist(0.0, 1.0);
    std::vector<double> hist_data(2000);
    for (auto& v: hist_data) v = ndist(rng);

    std::exponential_distribution<double> edist(1.5);
    std::vector<double> cum_data(1500);
    for (auto& v: cum_data) v = edist(rng);

    constexpr int R = 48, C = 48;
    std::vector<float> gauss(R * C), checker(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            double dr = (r - R / 2.0) / (R / 6.0), dc = (c - C / 2.0) / (C / 6.0);
            gauss[r * C + c] = static_cast<float>(std::exp(-(dr * dr + dc * dc)));
            checker[r * C + c] = static_cast<float>((r + c) % 2);
        }

    std::vector<double> pcurve_x(N), pcurve_y(N), ppts_x, ppts_y;
    std::normal_distribution<double> noise(0.0, 0.15);
    for (int i = 0; i < N; ++i) {
        pcurve_x[i] = -3.0 + i * 6.0 / (N - 1);
        pcurve_y[i] = 0.5 * pcurve_x[i] * pcurve_x[i] - 1.0;
    }
    for (int i = 0; i < 40; ++i) {
        double xv = -2.8 + i * 5.6 / 39.0;
        ppts_x.push_back(xv);
        ppts_y.push_back(0.5 * xv * xv - 1.0 + noise(rng));
    }

    // Scatter_z ring colored by z with its own colorbar, beside the A/B legend.
    constexpr int M = 80;
    std::vector<double> ring_x(M), ring_y(M), ring_z(M);
    for (int i = 0; i < M; ++i) {
        double t = i * 2.0 * M_PI / M;
        ring_x[i] = 3.0 * std::cos(t);
        ring_y[i] = 3.0 * std::sin(t);
        ring_z[i] = t;
    }

    // Error bars: an asymmetric whisker with a narrower symmetric box on the
    // line, and arrow caps on the bars with one side zeroed on the middle bar.
    constexpr int E = 10;
    std::vector<double> err_x(E), err_y(E), err_lo(E), err_hi(E), err_box(E), err_xc(E);
    for (int i = 0; i < E; ++i) {
        err_x[i] = i * 0.7;
        err_y[i] = 4.0 + 2.5 * std::sin(err_x[i]);
        err_lo[i] = 0.25 + 0.05 * i; // offsets from the point,
        err_hi[i] = 0.60 + 0.10 * i; // not absolute coordinates
        err_box[i] = 0.12 + 0.02 * i;
        err_xc[i] = 0.15;
    }
    const std::vector<double> err_bx = {0.7, 2.8, 4.9};
    const std::vector<double> err_bh = {3.0, 5.5, 2.2};
    const std::vector<double> err_blo = {0.6, 0.0, 0.4};
    const std::vector<double> err_bhi = {0.6, 0.9, 0.4};
    const std::vector<double> err_bbx = {0.3, 0.45, 0.2};

    // Scatter_z error bars are on the scatter_z cell below (a 2D box, no
    // whisker).
    std::vector<double> ring_xv(M), ring_yv(M);
    for (int i = 0; i < M; ++i) {
        ring_xv[i] = 0.25;
        ring_yv[i] = 0.4;
    }

    auto fig = sextant::Figure::create({
        .width = 1800, .height = 1000,
        .title = "Axes gallery", .supersample = 2
    });
    fig->suptitle("What Axes draws");

    fig->add_subplot(3, 4, 1)
            ->line(x, y_sin, {.color = sextant::Color::Blue, .linewidth = 4.0f, .name = "sin(x)"})
            .line(x, y_cos, {
                      .color = sextant::Color::Red, .linewidth = 2.0f,
                      .linestyle = sextant::LineStyle::Dashed, .name = "cos(x)"
                  })
            .line(pent_x, pent_y, {
                      .color = sextant::Color::Green, .linewidth = 3.0f,
                      .name = "loop", .loop = true
                  })
            .set_title("Line").legend().grid();

    fig->add_subplot(3, 4, 2)
            ->scatter(xa, ya, {.color = sextant::Color::Blue, .marker = sextant::MarkerStyle::Circle, .name = "A"})
            .scatter(xb, yb, {.color = sextant::Color::Orange, .marker = sextant::MarkerStyle::Diamond, .name = "B"})
            .set_title("Scatter").legend().grid();

    // Labelled, so the legend keys it.
    fig->add_subplot(3, 4, 3)
            ->bar(months, sales, {
                      .color = sextant::Color::Blue, .name = "monthly",
                      .edgecolor = sextant::Color::Black
                  })
            .set_title("Bar").legend().grid();

    // Both error-bar kinds in one cell: an asymmetric line whisker with a box
    // and a short x whisker, and symmetric arrow-capped bars with one side
    // zeroed.
    fig->add_subplot(4)
            ->line(err_x, err_y,
                   {
                       .x_cap_lo = err_xc, .y_cap_lo = err_lo, .y_cap_hi = err_hi,
                       .y_box_lo = err_box
                   },
                   {
                       .color = sextant::Color::Blue, .linewidth = 2.0f, .name = "measured",
                       .errorbar = {.color = sextant::Color::Gray, .capsize = 8.0f}
                   })
            .bar(err_bx, err_bh,
                 {.y_cap_lo = err_blo, .y_cap_hi = err_bhi, .y_box_lo = err_bbx},
                 {
                     .color = sextant::Color::Orange, .alpha = 0.5f, .name = "binned",
                     .errorbar = {
                         .linewidth = 1.5f, .capsize = 10.0f,
                         .capstyle = sextant::CapStyle::Arrow
                     }
                 })
            .set_title("Error bars").legend().grid();

    fig->add_subplot(3, 4, 5)
            ->hist(hist_data, 30, {.color = sextant::Color::Purple, .width = 1.0f}, {.density = true})
            .set_title("Histogram").grid();

    fig->add_subplot(3, 4, 6)
            ->hist(cum_data, 25, {.color = sextant::Color::Green, .width = 1.0f}, {.density = true, .cumulative = true})
            .set_title("Histogram (cumulative)").grid();

    // Labelled contours on the gaussian (white 1.5px; black vanishes into
    // viridis). Pan/zoom: they must stay glued to the colors. Named colorbar.
    fig->add_subplot(3, 4, 7)
            ->imshow(gauss, R, C, {
                         .cmap = sextant::Colormap::Viridis, .vmin = 0.0f, .vmax = 1.0f,
                         .colorbar = true, .name = "density",
                         .contours = {0.2, 0.4, 0.6, 0.8},
                         .contour_color = {1.0f, 1.0f, 1.0f, 0.9f},
                         .contour_linewidth = 1.5f,
                         .contour_labels = true
                     })
            .set_title("Heatmap + contours");

    fig->add_subplot(3, 4, 8)
            ->imshow(checker, R, C, {.vmin = 0.0f, .vmax = 1.0f, .origin = "upper"})
            .set_title("Heatmap (origin=upper)");

    fig->add_subplot(3, 4, 9)
            ->line(pcurve_x, pcurve_y, {.color = sextant::Color::Red, .linewidth = 2.0f, .name = "fit"})
            .scatter(ppts_x, ppts_y, {.color = sextant::Color::Blue, .size = 14.0f, .name = "samples", .alpha = 0.6f})
            .set_title("Line + scatter").legend().grid();

    // Marker shapes — one point per MarkerStyle
    {
        auto ax = fig->add_subplot(3, 4, 10);
        const sextant::MarkerStyle styles[] = {
            sextant::MarkerStyle::Circle, sextant::MarkerStyle::Square, sextant::MarkerStyle::Triangle,
            sextant::MarkerStyle::Cross, sextant::MarkerStyle::Plus, sextant::MarkerStyle::Diamond,
        };
        for (int i = 0; i < 6; ++i) {
            std::vector<double> mx = {static_cast<double>(i)}, my = {0.0};
            ax->scatter(mx, my, {.color = sextant::Color::Blue, .size = 30.0f, .marker = styles[i]});
        }
        ax->set_title("Marker shapes").set_xlim(-1, 6).set_ylim(-1, 1);
    }

    // scatter_z with both a legend key (white marker, black edge) and a
    // colorbar.
    fig->add_subplot(3, 4, 11)
            ->scatter(xa, ya, {.color = sextant::Color::Blue, .name = "A"})
            .scatter(xb, yb, {.color = sextant::Color::Orange, .name = "B"})
            // A 2D box: x_box wide, y_box tall, no whisker.
            .scatter_z(ring_x, ring_y, ring_z, {.x_box_lo = ring_xv, .y_box_lo = ring_yv}, {
                           .cmap = sextant::Colormap::Viridis, .size = 14.0f,
                           .vmin = 0.0f, .vmax = static_cast<float>(2.0 * M_PI),
                           .colorbar = true, .name = "phase",
                           .errorbar = {.linewidth = 0.75f}
                       })
            .set_title("scatter_z").legend().grid();

    // Two colour scales on one axes: each gets its own bar, sized for its own
    // numbers.
    fig->add_subplot(3, 4, 12)
            ->imshow(gauss, R, C, {
                         .vmin = 0.0f, .vmax = 1.0f,
                         .colorbar = true, .name = "field"
                     })
            .scatter_z(ring_x, ring_y, ring_z, {
                           .size = 22.0f, .marker = sextant::MarkerStyle::Square,
                           .vmin = 0.0f, .vmax = static_cast<float>(2.0 * M_PI),
                           .colorbar = true, .name = "phase"
                       })
            .set_title("Two colorbars").legend();

    fig->show(true);
    fig->savefig("test_axes_gallery.png");
    fig->savefig("test_axes_gallery.svg");
    printf("Saved: test_axes_gallery.png, test_axes_gallery.svg\n");
}

// 3D gallery — one subplot per 3D kind, as an overview (test_translucent3d is
// the ordering harness).
static void test_axes3d_gallery() {
    // Two grids, small enough that the SVG painter finishes quickly.
    constexpr int NU = 9, NV = 7;
    std::vector<double> gu(NU), gv(NV), gauss(NU * NV);
    for (int i = 0; i < NU; ++i) gu[i] = 400.0 + i * 25.0; // wavelength, nm
    for (int j = 0; j < NV; ++j) gv[j] = -1.5 + j * 0.5; // offset
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j) {
            const double a = (i - NU / 2.0) / 2.2, b = (j - NV / 2.0) / 1.8;
            gauss[i * NV + j] = 20.0 * std::exp(-(a * a + b * b));
        }

    constexpr int SU = 24, SV = 24;
    std::vector<double> su(SU), sv(SV), ripple(SU * SV), dome(SU * SV);
    for (int i = 0; i < SU; ++i) su[i] = -3.0 + i * 6.0 / (SU - 1);
    for (int j = 0; j < SV; ++j) sv[j] = -3.0 + j * 6.0 / (SV - 1);
    for (int i = 0; i < SU; ++i)
        for (int j = 0; j < SV; ++j) {
            const double r = std::sqrt(su[i] * su[i] + sv[j] * sv[j]);
            ripple[i * SV + j] = 2.0 * std::exp(-r * 0.5) * std::cos(r * 2.2);
            dome[i * SV + j] = 3.0 - 0.3 * r * r;
        }

    constexpr int R = 24, C = 24;
    std::vector<float> field(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            const double dr = (r - R / 2.0) / (R / 3.0), dc = (c - C / 2.0) / (C / 3.0);
            field[r * C + c] = static_cast<float>(std::exp(-(dr * dr + dc * dc)));
        }

    constexpr int L = 60;
    std::vector<double> lx(L), ly(L), px(20), py(20), bx(6), bh(6);
    for (int i = 0; i < L; ++i) {
        lx[i] = i * 6.0 / (L - 1);
        ly[i] = 2.0 + 1.4 * std::sin(lx[i] * 1.3);
    }
    for (int i = 0; i < 20; ++i) {
        px[i] = i * 6.0 / 19.0;
        py[i] = 1.0 + 0.5 * std::cos(px[i] * 2.0);
    }
    for (int i = 0; i < 6; ++i) {
        bx[i] = i + 0.5;
        bh[i] = 0.6 + 0.25 * i;
    }

    // Cell 7: a flat cloud on a helix and a colormapped one on a lattice.
    constexpr int HN = 40;
    std::vector<double> hx(HN), hy(HN), hz(HN);
    for (int i = 0; i < HN; ++i) {
        const double t = i * 6.2831853 * 1.5 / (HN - 1);
        hx[i] = 2.4 * std::cos(t);
        hy[i] = 2.4 * std::sin(t);
        hz[i] = -2.5 + 5.0 * i / (HN - 1);
    }
    std::vector<double> cx3, cy3, cz3, cval;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 3; ++k) {
                const double x = -2.4 + i * 1.6, y = -2.4 + j * 1.6, z = -2.0 + k * 2.0;
                cx3.push_back(x);
                cy3.push_back(y);
                cz3.push_back(z);
                cval.push_back(60.0 * std::exp(-(x * x + y * y + z * z) / 9.0));
            }

    // Cell 8: a cloud spread either side of a sheet standing at x = 0.
    std::vector<double> dx, dy, dz;
    for (int i = 0; i < 90; ++i) {
        const double t = i * 0.7;
        dx.push_back(2.6 * std::sin(t * 0.9));
        dy.push_back(2.4 * std::cos(t * 0.6));
        dz.push_back(-2.2 + 4.4 * (i % 9) / 8.0);
    }
    const std::vector<double> sheet_u{-3.0, 0.0, 3.0}; // y
    const std::vector<double> sheet_v{-3.0, 0.0, 3.0}; // z
    const std::vector<double> sheet_h(9, 0.0); // x = 0

    auto fig = sextant::Figure::create({
        .width = 2200, .height = 1000,
        .title = "Axes3D gallery",
        .subplot_col_gap = 24.0f, .subplot_row_gap = 24.0f,
        .supersample = 2
    });
    fig->suptitle("What Axes3D draws");

    // 1 — bar3d: a 2D histogram standing up, keyed in the legend.
    fig->add_subplot3d(2, 4, 1)
            ->bar3d(sextant::PlaneOrientation::XY, gu, gv, gauss,
                    {.color = sextant::Color::Blue, .name = "counts"})
            .set_title("bar3d")
            .set_xtitle("wavelength (nm)").set_ytitle("offset").set_ztitle("counts")
            .set_view(-55.0, 25.0)
            .legend();

    // 2 — surface colored by height, with a named colorbar.
    fig->add_subplot3d(2, 4, 2)
            ->surface(sextant::PlaneOrientation::XY, su, sv, ripple,
                      {
                          .colormap = true, .colorbar = true, .name = "amplitude",
                          .shading = 0.35f
                      })
            .set_title("surface")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .set_view(-50.0, 28.0);

    // 3 — surface with a flat color and a wireframe.
    fig->add_subplot3d(2, 4, 3)
            ->surface(sextant::PlaneOrientation::XY, su, sv, dome,
                      {
                          .color = sextant::Color::Cyan, .shading = 0.5f,
                          .edges = true, .edge_linewidth = 0.8f
                      })
            .set_title("surface + wireframe")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .set_view(-40.0, 22.0);

    // 4 — Plane2D carrying a heatmap; its colorbar is beside the cell.
    {
        auto ax = fig->add_subplot3d(2, 4, 4);
        ax->set_title("Plane2D — slices")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_zlim(0.0, 3.0)
                .set_view(-60.0, 20.0);
        ax->plane(sextant::PlaneOrientation::XY, 0.2)
                ->heatmap(field, R, C, {0.0, 6.0}, {0.0, 6.0},
                          {.vmin = 0.0f, .vmax = 1.0f, .colorbar = true, .name = "intensity"});
        // Two more translucent slices, stacked.
        for (double z: {1.4, 2.6})
            ax->plane(sextant::PlaneOrientation::XY, z, {.alpha = 0.55f})
                    ->heatmap(field, R, C, {0.0, 6.0}, {0.0, 6.0}, {.vmin = 0.0f, .vmax = 1.0f});
    }

    // 5 — a plane carrying every 2D kind, with a legend.
    {
        auto ax = fig->add_subplot3d(2, 4, 5);
        ax->set_title("Plane2D — the 2D kinds")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-62.0, 18.0);
        auto pl = ax->plane(sextant::PlaneOrientation::XY, 0.0);
        pl->bar(bx, bh, {.color = sextant::Color::Cyan, .alpha = 0.8f, .name = "binned"});
        pl->line(lx, ly, {.color = sextant::Color::Red, .linewidth = 2.5f, .name = "fit"});
        pl->scatter(px, py, {
                        .color = sextant::Color::Blue, .size = 26.0f,
                        .marker = sextant::MarkerStyle::Diamond, .name = "samples"
                    });
        ax->legend();
    }

    // 6 — the kinds together under perspective, two colour scales, and
    // translucent bars in front of a sheet (peeled in PNG, split in SVG).
    {
        auto ax = fig->add_subplot3d(2, 4, 6);
        ax->set_title("Together, in perspective")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_projection(sextant::Projection::Perspective)
                .set_fov(38.0)
                .set_view(-48.0, 24.0);
        ax->surface(sextant::PlaneOrientation::XY, su, sv, ripple,
                    {
                        .colormap = true, .colorbar = true, .name = "sheet",
                        .alpha = 0.85f, .shading = 0.35f
                    });
        ax->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.7f})
                ->heatmap(field, R, C, {-3.0, 3.0}, {-2.0, 3.0},
                          {.vmin = 0.0f, .vmax = 1.0f, .colorbar = true, .name = "slice"});
        ax->legend();
    }

    // 7 — scatter3d with both key forms side by side: a flat swatch, and a
    // colormapped marker in white with a black edge.
    {
        auto ax = fig->add_subplot3d(2, 4, 7);
        ax->set_title("scatter3d")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-52.0, 22.0);
        ax->scatter3d(hx, hy, hz,
                      {
                          .color = sextant::Color::Orange, .size = 26.0f,
                          .marker = sextant::MarkerStyle::Diamond, .name = "measured"
                      });
        ax->scatter3d(cx3, cy3, cz3, cval,
                      {
                          .size = 30.0f, .marker = sextant::MarkerStyle::Circle,
                          .colorbar = true, .name = "energy"
                      });
        ax->legend();
    }

    // 8 — a cloud through a translucent sheet under perspective with
    // `depthshade`; far-side markers are behind the sheet in both outputs.
    {
        auto ax = fig->add_subplot3d(2, 4, 8);
        ax->set_title("depth shading, through a sheet")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_projection(sextant::Projection::Perspective)
                .set_fov(40.0)
                .set_view(-58.0, 18.0);
        ax->surface(sextant::PlaneOrientation::YZ, sheet_u, sheet_v, sheet_h,
                    {.color = sextant::Color::Cyan, .alpha = 0.45f, .shading = 0.3f});
        ax->scatter3d(dx, dy, dz,
                      {
                          .color = sextant::Color::Purple, .size = 22.0f,
                          .depthshade = 0.7f, .name = "cloud"
                      });
        ax->legend();
    }

    fig->show(true);
    fig->savefig("test_axes3d_gallery.png");
    fig->savefig("test_axes3d_gallery.svg");
    printf("Saved: test_axes3d_gallery.png, test_axes3d_gallery.svg\n");
}

// -------------------------------------------------------------------------
// Translucency across every pair of 3D kinds
// -------------------------------------------------------------------------
// Six cells, one per pair of kinds, each built so the two objects genuinely
// interleave in depth. Orbit slowly, including past both poles and near zero
// elevation:
//   - nothing may pop (no whole-object order flips);
//   - both sides of every crossing (cells 1, 3, 5) must be right;
//   - colors must not depend on insertion order.
// Compare with SEXTANT_PEEL_LAYERS=0 (whole-object order): every cell should
// change. The SVG is saved too and is expected to be wrong in places (the
// vector path has no depth test).
// -------------------------------------------------------------------------
// line3d gallery: four cells, each judged by eye.
static void test_line3d_gallery() {
    auto fig = sextant::Figure::create({
        .width = 1500, .height = 820,
        .title = "sextant — line3d"
    });
    fig->suptitle("line3d — a path in the scene");

    // ---- A spiral colored by its parameter: every color along it must be on
    // the colorbar (values are interpolated, not RGB).
    std::vector<double> sx, sy, sz, st;
    for (int i = 0; i <= 320; ++i) {
        const double t = i / 320.0;
        const double a = t * 6.0 * 3.14159265358979;
        const double r = 0.25 + 0.75 * t;
        sx.push_back(r * std::cos(a));
        sy.push_back(r * std::sin(a));
        sz.push_back(-1.0 + 2.0 * t);
        st.push_back(t * 100.0);
    } {
        auto ax = fig->add_subplot3d(2, 2, 1);
        ax->set_title("a coloured trajectory")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-58.0, 22.0);
        ax->line3d(sx, sy, sz, st,
                   {.linewidth = 3.0f, .colorbar = true, .name = "time"});
        ax->legend();
    }

    // ---- `loop` and the miter: a thick five-pointed star; unmitered joins
    // would show wedges at its ten corners.
    {
        std::vector<double> px, py, pz;
        for (int k = 0; k < 10; ++k) {
            const double a = k * 3.14159265358979 / 5.0 - 1.5707963;
            const double r = (k % 2 == 0) ? 1.0 : 0.42;
            px.push_back(r * std::cos(a));
            py.push_back(r * std::sin(a));
            pz.push_back(0.0);
        }
        auto ax = fig->add_subplot3d(2, 2, 2);
        ax->set_title("loop, and the mitered bends")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-20.0, 62.0);
        ax->line3d(px, py, pz,
                   {
                       .color = sextant::Color::Purple, .linewidth = 9.0f,
                       .loop = true, .name = "closed"
                   });
        ax->legend();
    }

    // ---- Scene-space width vs pixel-size markers: two paths of equal width at
    // opposite ends of the box's depth, with equal-size markers on both. Under
    // perspective the near path is thicker; the markers match.
    {
        const std::vector<double> ny{-0.7, 0.7}, fy{-0.7, 0.7};
        const std::vector<double> nx{0.8, 0.8}, fx{-0.8, -0.8};
        const std::vector<double> nz{0.3, 0.3}, fz{-0.3, -0.3};
        auto ax = fig->add_subplot3d(2, 2, 3);
        ax->set_title("a width in the scene, a marker in pixels")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_projection(sextant::Projection::Perspective)
                .set_fov(38.0)
                .set_view(-22.0, 16.0);
        ax->line3d(nx, ny, nz,
                   {
                       .color = sextant::Color::Blue, .linewidth = 9.0f,
                       .name = "near path"
                   });
        ax->line3d(fx, fy, fz,
                   {
                       .color = sextant::Color::Blue, .linewidth = 9.0f,
                       .show_legend = false
                   });
        ax->scatter3d(nx, ny, nz,
                      {
                          .color = sextant::Color::Orange, .size = 18.0f,
                          .marker = sextant::MarkerStyle::Square,
                          .name = "markers, both depths"
                      });
        ax->scatter3d(fx, fy, fz,
                      {
                          .color = sextant::Color::Orange, .size = 18.0f,
                          .marker = sextant::MarkerStyle::Square,
                          .show_legend = false
                      });
        ax->legend();
    }

    // ---- A path woven through a translucent sheet, with `depthshade`: the
    // stretches behind it stay behind it in both outputs.
    {
        std::vector<double> wu{-1.0, 0.0, 1.0}, wv{-1.0, 0.0, 1.0};
        std::vector<double> wh(9, 0.0);
        std::vector<double> wx, wy, wz;
        for (int i = 0; i <= 24; ++i) {
            const double t = i / 24.0;
            wx.push_back(-1.0 + 2.0 * t);
            wy.push_back(0.9 * std::sin(t * 4.0 * 3.14159265358979));
            wz.push_back(0.8 * std::cos(t * 3.0 * 3.14159265358979));
        }
        auto ax = fig->add_subplot3d(2, 2, 4);
        ax->set_title("through a sheet, with depth shading")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_projection(sextant::Projection::Perspective)
                .set_fov(44.0)
                .set_view(-54.0, 18.0);
        ax->surface(sextant::PlaneOrientation::XY, wu, wv, wh,
                    {.color = sextant::Color::Cyan, .alpha = 0.4f, .shading = 0.25f});
        ax->line3d(wx, wy, wz,
                   {
                       .color = sextant::Color::Red, .linewidth = 6.0f,
                       .depthshade = 0.75f, .name = "woven"
                   });
        ax->legend();
    }

    fig->show(true);
    fig->savefig("test_line3d_gallery.png");
    fig->savefig("test_line3d_gallery.svg");
    printf("Saved: test_line3d_gallery.png, test_line3d_gallery.svg\n");
}

// -------------------------------------------------------------------------
// 3D error bars. What to look for:
//
//   1. A cloud with asymmetric flat z caps and one-sided arrow x caps. Orbit:
//      caps turn with their whisker; an end-on x whisker disappears.
//   2. Blocks: box data on y and z (x is `boxwidth`), and on all three axes.
//      Translucent, so the marker inside shows.
//   3. A path with caps and blocks under perspective: near bars are larger as
//      a whole.
//   4. A colormapped cloud: error bars default to black and darken with depth.
static void test_errorbar3d_gallery() {
    auto fig = sextant::Figure::create({
        .width = 1500, .height = 860,
        .title = "sextant — 3D error bars"
    });
    fig->suptitle("3D error bars — whiskers, caps and blocks");

    const std::vector<double> x{-0.8, -0.3, 0.2, 0.7};
    const std::vector<double> y{0.6, -0.4, 0.3, -0.7};
    const std::vector<double> z{-0.5, 0.2, 0.6, -0.1}; {
        const std::vector<double> zlo{0.2, 0.35, 0.15, 0.3}, zhi{0.4, 0.15, 0.3, 0.25};
        const std::vector<double> xhi{0.3, 0.0, 0.25, 0.2};
        auto ax = fig->add_subplot3d(2, 2, 1);
        ax->set_title("caps: flat on z, arrow one-sided on x")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-58.0, 22.0);
        ax->scatter3d(x, y, z, {.z_cap_lo = zlo, .z_cap_hi = zhi},
                      {
                          .color = sextant::Color::Blue, .size = 12.0f, .name = "flat z",
                          .errorbar = {.linewidth = 2.0f, .capsize = 12.0f}
                      });
        std::vector<double> x2 = x;
        for (double& v: x2) v -= 0.1;
        ax->scatter3d(x2, z, y, {.x_cap_hi = xhi},
                      {
                          .color = sextant::Color::Orange, .size = 12.0f, .name = "arrow x",
                          .errorbar = {
                              .linewidth = 2.0f, .capsize = 12.0f,
                              .capstyle = sextant::CapStyle::Arrow
                          }
                      });
        ax->legend();
    } {
        const std::vector<double> ly{-0.6, 0.6}, lz{0.0, 0.0};
        const std::vector<double> left_x{-0.5, -0.5}, right_x{0.5, 0.5};
        const std::vector<double> b2{0.25, 0.35}, b3{0.2, 0.3};
        auto ax = fig->add_subplot3d(2, 2, 2);
        ax->set_title("blocks: boxwidth on x (left), data on all three (right)")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-40.0, 25.0);
        ax->scatter3d(left_x, ly, lz, {.y_box_lo = b2, .z_box_lo = b3},
                      {
                          .color = sextant::Color::Green, .size = 14.0f, .name = "y, z data",
                          .errorbar = {.boxwidth = 30.0f}
                      });
        ax->scatter3d(right_x, ly, lz, {.x_box_lo = b3, .y_box_lo = b2, .z_box_hi = b3},
                      {
                          .color = sextant::Color::Red, .size = 14.0f, .name = "x, y, z data",
                          .errorbar = {.boxwidth = 30.0f}
                      });
        ax->legend();
    } {
        std::vector<double> px, py, pz, zc, yb;
        for (int i = 0; i < 9; ++i) {
            const double t = i / 8.0;
            px.push_back(-1.0 + 2.0 * t);
            py.push_back(0.5 * std::sin(t * 3.14159265358979 * 2.0));
            pz.push_back(0.4 * std::cos(t * 3.14159265358979 * 1.5));
            zc.push_back(0.15 + 0.1 * t);
            yb.push_back(0.08);
        }
        auto ax = fig->add_subplot3d(2, 2, 3);
        ax->set_title("a path under perspective: near bars bigger as a whole")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_projection(sextant::Projection::Perspective)
                .set_fov(46.0)
                .set_view(-12.0, 18.0);
        ax->line3d(px, py, pz, {.y_box_lo = yb, .z_cap_lo = zc},
                   {
                       .color = sextant::Color::Purple, .linewidth = 3.0f, .name = "path",
                       .errorbar = {.linewidth = 2.0f, .capsize = 14.0f, .boxwidth = 14.0f}
                   });
        ax->legend();
    } {
        std::vector<double> cx, cy, cz, cv, e;
        for (int i = 0; i < 16; ++i) {
            const double a = i * 0.785398;
            cx.push_back(0.8 * std::cos(a) * (0.5 + i / 30.0));
            cy.push_back(0.8 * std::sin(a) * (0.5 + i / 30.0));
            cz.push_back(-0.8 + 0.1 * i);
            cv.push_back(i);
            e.push_back(0.12);
        }
        auto ax = fig->add_subplot3d(2, 2, 4);
        ax->set_title("colormapped: black bars, depth-shaded")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-50.0, 20.0);
        ax->scatter3d(cx, cy, cz, cv, {.x_cap_lo = e, .y_cap_lo = e, .z_cap_lo = e},
                      {
                          .size = 12.0f, .depthshade = 0.7f, .colorbar = true, .name = "value",
                          .errorbar = {.capsize = 8.0f}
                      });
        ax->legend();
    }

    fig->show(true);
    fig->savefig("test_errorbar3d_gallery.png");
    fig->savefig("test_errorbar3d_gallery.svg");
    printf("Saved: test_errorbar3d_gallery.png, test_errorbar3d_gallery.svg\n");
}

// -------------------------------------------------------------------------
// surface_tri gallery. What to look for:
//
//   1. Color doesn't jump at an edge; the light does (scattered samples).
//   2. The same with large facets: no color steps at facet boundaries.
//   3. A Mobius strip (no consistent winding): it reads as a continuous
//      ribbon because shading is two-sided. Wireframe on.
//   4. Translucency and self-occlusion: compare the PNG with the SVG, and with
//      SEXTANT_PEEL_LAYERS=1.
static void test_surface_tri_gallery() {
    auto fig = sextant::Figure::create({
        .width = 1500, .height = 820,
        .title = "sextant — surface_tri"
    });
    fig->suptitle("surface_tri — a sheet on a triangulated mesh");

    constexpr double kPi = 3.14159265358979;

    // ---- 1. Scattered samples of a smooth field, triangulated at ingest.
    {
        std::vector<double> x, y, z, c;
        unsigned seed = 7u;
        auto rnd = [&] {
            seed = seed * 1103515245u + 12345u;
            return ((seed >> 16) & 0x7fff) / 32767.0;
        };
        for (int i = 0; i < 420; ++i) {
            const double u = -1.0 + 2.0 * rnd();
            const double v = -1.0 + 2.0 * rnd();
            const double r = std::sqrt(u * u + v * v);
            const double h = 0.85 * std::exp(-1.8 * r * r) * std::cos(5.5 * r);
            x.push_back(u);
            y.push_back(v);
            z.push_back(h);
            c.push_back(h);
        }
        auto ax = fig->add_subplot3d(2, 2, 1);
        ax->set_title("a scan, triangulated at ingest")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-58.0, 26.0);
        ax->surface_tri(x, y, z, sextant::PlaneOrientation::XY, c,
                        {.colorbar = true, .name = "height", .shading = 0.35f});
    }

    // ---- 2. A coarse dome with a color ramp diagonal to the triangulation.
    {
        constexpr int N = 5;
        std::vector<double> x, y, z, c;
        std::vector<std::uint32_t> tri;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                const double u = -1.0 + 2.0 * i / (N - 1);
                const double v = -1.0 + 2.0 * j / (N - 1);
                x.push_back(u);
                y.push_back(v);
                z.push_back(0.9 * (1.0 - 0.45 * (u * u + v * v)));
                c.push_back(u + v);
            }
        for (std::uint32_t i = 0; i + 1 < N; ++i)
            for (std::uint32_t j = 0; j + 1 < N; ++j) {
                const std::uint32_t a = i * N + j;
                tri.push_back(a);
                tri.push_back(a + N);
                tri.push_back(a + N + 1);
                tri.push_back(a);
                tri.push_back(a + N + 1);
                tri.push_back(a + 1);
            }
        auto ax = fig->add_subplot3d(2, 2, 2);
        ax->set_title("colour per vertex, light per face")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-40.0, 30.0);
        ax->surface_tri(x, y, z, tri, c,
                        {.colorbar = true, .name = "x + y", .shading = 0.85f});
    }

    // ---- 3. A Mobius strip. Wireframe on.
    {
        constexpr int M = 44;
        std::vector<double> x, y, z;
        std::vector<std::uint32_t> tri;
        for (int k = 0; k < M; ++k) {
            const double t = 2.0 * kPi * k / M;
            for (int e = 0; e < 2; ++e) {
                const double w = (e == 0 ? -0.30 : 0.30);
                const double rad = 1.0 + w * std::cos(t * 0.5);
                x.push_back(rad * std::cos(t));
                y.push_back(rad * std::sin(t));
                z.push_back(w * std::sin(t * 0.5));
            }
        }
        for (std::uint32_t k = 0; k < M; ++k) {
            const std::uint32_t a = k * 2, b = a + 1;
            // The closing rung joins the flipped first one (Mobius, not a
            // cylinder).
            const std::uint32_t nk = (k + 1) % M;
            const bool wrap = (k + 1 == M);
            const std::uint32_t c = wrap ? nk * 2 + 1 : nk * 2;
            const std::uint32_t d = wrap ? nk * 2 : nk * 2 + 1;
            tri.push_back(a);
            tri.push_back(b);
            tri.push_back(d);
            tri.push_back(a);
            tri.push_back(d);
            tri.push_back(c);
        }
        auto ax = fig->add_subplot3d(2, 2, 3);
        ax->set_title("no consistent winding, and the wireframe")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-36.0, 34.0);
        ax->surface_tri(x, y, z, tri,
                        {
                            .color = sextant::Color::Orange, .name = "Mobius",
                            .shading = 0.75f, .edges = true,
                            .edge_alpha = 0.7f, .edge_linewidth = 1.0f
                        });
        ax->legend();
    }

    // ---- 4. Translucent, folded over itself, and crossing an opaque sheet.
    {
        constexpr int NU = 72, NV = 8;
        std::vector<double> x, y, z, c;
        std::vector<std::uint32_t> tri;
        for (int i = 0; i < NU; ++i)
            for (int j = 0; j < NV; ++j) {
                // A two-turn helical ramp, so the self-overlap is visible.
                const double t = 4.0 * kPi * i / (NU - 1);
                const double rad = 0.22 + 0.78 * j / (NV - 1);
                x.push_back(rad * std::cos(t));
                y.push_back(rad * std::sin(t));
                z.push_back(-0.75 + 0.12 * t);
                c.push_back(t);
            }
        for (std::uint32_t i = 0; i + 1 < NU; ++i)
            for (std::uint32_t j = 0; j + 1 < NV; ++j) {
                const std::uint32_t a = i * NV + j;
                tri.push_back(a);
                tri.push_back(a + NV);
                tri.push_back(a + NV + 1);
                tri.push_back(a);
                tri.push_back(a + NV + 1);
                tri.push_back(a + 1);
            }
        const std::vector<double> su{-1.0, 0.0, 1.0}, sv{-1.0, 0.0, 1.0};
        const std::vector<double> sh(9, 0.0);

        auto ax = fig->add_subplot3d(2, 2, 4);
        ax->set_title("translucent, and occluding itself")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_projection(sextant::Projection::Perspective)
                .set_fov(42.0)
                .set_view(-52.0, 20.0);
        ax->surface(sextant::PlaneOrientation::XY, su, sv, sh,
                    {
                        .color = sextant::Color::Cyan, .name = "sheet",
                        .shading = 0.2f
                    });
        ax->surface_tri(x, y, z, tri, c,
                        {
                            .colorbar = true, .name = "angle", .alpha = 0.55f,
                            .shading = 0.4f
                        });
        ax->legend();
    }

    fig->show(true);
    fig->savefig("test_surface_tri_gallery.png");
    fig->savefig("test_surface_tri_gallery.svg");
    printf("Saved: test_surface_tri_gallery.png, test_surface_tri_gallery.svg\n");
}

static void test_translucent3d() {
    constexpr int NU = 13, NV = 13;
    std::vector<double> gx(NU), gy(NV), ripple(NU * NV), bowl(NU * NV);
    for (int i = 0; i < NU; ++i) gx[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (NU - 1);
    for (int j = 0; j < NV; ++j) gy[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (NV - 1);
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j) {
            const double x = gx[static_cast<std::size_t>(i)];
            const double y = gy[static_cast<std::size_t>(j)];
            const double r = std::hypot(x, y);
            ripple[static_cast<std::size_t>(i * NV + j)] = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
            // A saddle, so the sheets in cell 3 cross along two lines.
            bowl[static_cast<std::size_t>(i * NV + j)] = 0.22 * (x * x - y * y);
        }

    // A field for the planes, so crossing contents visibly change over.
    constexpr int NX = 40, NY = 40;
    auto slice_at = [&](double z) {
        std::vector<float> m(static_cast<std::size_t>(NY) * NX);
        for (int r = 0; r < NY; ++r)
            for (int c = 0; c < NX; ++c) {
                const double x = -3.0 + 6.0 * c / (NX - 1);
                const double y = -3.0 + 6.0 * r / (NY - 1);
                m[static_cast<std::size_t>(r) * NX + c] =
                        static_cast<float>(std::sin(x + z) * std::cos(y - z));
            }
        return m;
    };

    auto tr = sextant::Figure::create({
        .width = 1500, .height = 950,
        .title = "translucent 3D", .subplot_col_gap = 0.0f
    });
    tr->suptitle("Translucent blending, every pair of kinds");

    const sextant::Range ext{-3.0, 3.0};

    // The two planes use different ends of viridis, so crossings show a hue
    // change.
    const sextant::HeatmapOptions kCool{.vmin = -1.2f, .vmax = 5.0f};
    const sextant::HeatmapOptions kWarm{.vmin = -5.0f, .vmax = 1.2f};

    // 1. Plane x plane: two slices crossing at right angles.
    auto c1 = tr->add_subplot3d(2, 3, 1);
    c1->set_title("Plane x plane (crossing)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z");
    c1->plane(sextant::PlaneOrientation::XY, 0.0, {.alpha = 0.6f})
            ->heatmap(slice_at(0.0), NY, NX, ext, ext, kCool);
    c1->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
            ->heatmap(slice_at(1.5), NY, NX, ext, ext, kWarm);

    // 2. Bar x bar: the same footprint offset by half a cell, so bars
    // alternate along any horizontal ray.
    std::vector<double> gx2(NU), gy2(NV);
    for (int i = 0; i < NU; ++i) gx2[static_cast<std::size_t>(i)] = gx[static_cast<std::size_t>(i)] + 0.25;
    for (int j = 0; j < NV; ++j) gy2[static_cast<std::size_t>(j)] = gy[static_cast<std::size_t>(j)] + 0.25;
    auto c2 = tr->add_subplot3d(2, 3, 2);
    c2->set_title("Bar x bar (interleaved)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z");
    c2->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {
                  .color = sextant::Color::Orange, .alpha = 0.5f,
                  .width = 0.45f, .depth = 0.45f, .bottom = -1.5
              });
    c2->bar3d(sextant::PlaneOrientation::XY, gx2, gy2, bowl,
              {
                  .color = sextant::Color::Blue, .alpha = 0.5f,
                  .width = 0.45f, .depth = 0.45f, .bottom = -1.5
              });

    // 3. Surface x surface: a ripple and a saddle crossing along two curves.
    auto c3 = tr->add_subplot3d(2, 3, 3);
    c3->set_title("Surface x surface (two crossings)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z");
    c3->surface(sextant::PlaneOrientation::XY, gx, gy, ripple,
                {.color = sextant::Color::from_hex(0xff5533), .alpha = 0.55f});
    c3->surface(sextant::PlaneOrientation::XY, gx, gy, bowl,
                {.color = sextant::Color::from_hex(0x3388ff), .alpha = 0.55f});

    // 4. Plane x bar: a vertical cut through the grid.
    auto c4 = tr->add_subplot3d(2, 3, 4);
    c4->set_title("Plane x bar (a cut through the grid)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z");
    c4->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {
                  .color = sextant::Color::Green, .alpha = 0.5f,
                  .width = 0.8f, .depth = 0.8f, .bottom = -1.5
              });
    c4->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
            ->heatmap(slice_at(0.6), NY, NX, ext, ext, kWarm);

    // 5. Plane x surface: the sheet is above the plane in the middle and below
    // it at the rim.
    auto c5 = tr->add_subplot3d(2, 3, 5);
    c5->set_title("Plane x surface (crossing)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z");
    c5->surface(sextant::PlaneOrientation::XY, gx, gy, ripple,
                {.colormap = true, .cmap = sextant::Colormap::Viridis, .alpha = 0.6f});
    c5->plane(sextant::PlaneOrientation::XY, 0.35, {.alpha = 0.6f})
            ->heatmap(slice_at(0.35), NY, NX, ext, ext, kWarm);

    // 6. Bar x surface: the sheet at half the bars' height, inside every bar,
    // so no whole-object order works from any camera.
    std::vector<double> midway(NU * NV);
    for (std::size_t k = 0; k < midway.size(); ++k) midway[k] = -1.5 + 0.5 * ripple[k];
    auto c6 = tr->add_subplot3d(2, 3, 6);
    c6->set_title("Bar x surface (sheet inside the bars)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z");
    c6->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {
                  .color = sextant::Color::Orange, .alpha = 0.5f,
                  .width = 0.7f, .depth = 0.7f, .bottom = -1.5
              });
    c6->surface(sextant::PlaneOrientation::XY, gx, gy, midway,
                {.colormap = true, .cmap = sextant::Colormap::Viridis, .alpha = 0.55f});

    tr->savefig("test_translucent3d.png");
    tr->savefig("test_translucent3d.svg");
    printf("Saved: test_translucent3d.png/.svg\n");
    printf("Orbit each cell, especially to elevation ~0. Re-run with\n"
        "SEXTANT_PEEL_LAYERS=0 to see the whole-object order it replaces.\n");

    tr->show();
}

static void test_savefig() {
    constexpr int N = 80;
    std::vector<double> x(N), y_sin(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y_sin[i] = std::sin(x[i]);
    }

    const std::vector<double> months = {1, 2, 3, 4, 5, 6};
    const std::vector<double> sales = {42, 55, 61, 49, 78, 83};

    constexpr int R = 24, C = 24;
    std::vector<float> gauss(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            double dr = (r - R / 2.0) / (R / 6.0), dc = (c - C / 2.0) / (C / 6.0);
            gauss[r * C + c] = static_cast<float>(std::exp(-(dr * dr + dc * dc)));
        }

    auto fig = sextant::Figure::create({.width = 1400, .height = 480, .title = "Headless export"});
    fig->add_subplot(1, 3, 1)
            ->line(x, y_sin, {.color = sextant::Color::Blue, .linewidth = 2.0f, .name = "sin(x)"})
            .set_title("Line").set_xtitle("x").set_ytitle("y").legend().grid();
    fig->add_subplot(1, 3, 2)
            ->bar(months, sales, {.color = sextant::Color::Cyan, .edgecolor = sextant::Color::Black})
            .set_title("Bar").set_xtitle("month").set_ytitle("units").grid();
    // The gallery's contours through the headless path (no window, no GL).
    fig->add_subplot(1, 3, 3)
            ->imshow(gauss, R, C, {
                         .cmap = sextant::Colormap::Viridis, .vmin = 0.0f, .vmax = 1.0f, .colorbar = true,
                         .contours = {0.2, 0.4, 0.6, 0.8},
                         .contour_color = {1.0f, 1.0f, 1.0f, 0.9f},
                         .contour_linewidth = 1.5f,
                         .contour_labels = true
                     })
            .set_title("Heatmap + contours");

    fig->savefig("test_export.png");
    fig->savefig("test_export.svg");
    printf("Saved: test_export.png, test_export.svg\n");
}

// 3. Non-blocking show — window runs in background while main thread continues
static void test_show_nonblocking() {
    constexpr int N = 100;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }

    auto fig = sextant::Figure::create({.width = 700, .height = 450, .title = "Non-blocking show"});
    fig->axes()
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f, .name = "sin(x)"})
            .set_title("Non-blocking window — main thread keeps running")
            .set_xtitle("x")
            .set_ytitle("y")
            .grid();

    fig->show(false); // returns immediately; window lives on background thread
    printf("[main] show(false) returned  is_open=%s\n", fig->is_open() ? "true" : "false");

    for (int i = 1; i <= 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        printf("[main] tick %d  is_open=%s\n", i, fig->is_open() ? "true" : "false");
    }

    fig->close();
    printf("[main] close() called  is_open=%s\n", fig->is_open() ? "true" : "false");
}

// 4. refresh() before show() — must throw std::logic_error
static void test_refresh_before_show() {
    auto fig = sextant::Figure::create();
    bool threw = false;
    try {
        fig->refresh();
    } catch (const std::logic_error&) {
        threw = true;
    }
    printf("[main] refresh() before show() threw logic_error: %s\n",
           threw ? "true" : "false");
}

// 5. Live refresh — mutate Axes from the main thread while the render thread
// runs.
static void test_live_refresh() {
    constexpr int N = 100;
    std::vector<double> x(N);
    for (int i = 0; i < N; ++i) x[i] = i * 2.0 * M_PI / N;

    auto fig = sextant::Figure::create({.width = 700, .height = 450, .title = "Live refresh"});
    auto ax = fig->axes();

    std::vector<double> y0(N);
    for (int i = 0; i < N; ++i) y0[i] = std::sin(x[i]);
    ax->line(x, y0, {.color = sextant::Color::Blue, .linewidth = 2.0f})
            .set_title("Live refresh test");

    fig->show(false);

    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ax->cla();
        std::vector<double> y(N);
        for (int j = 0; j < N; ++j) y[j] = std::sin(x[j] + i * 0.1);
        ax->line(x, y, {.color = sextant::Color::Red, .linewidth = 2.0f});
        fig->refresh();
    }

    fig->close();
    printf("[main] live refresh test done\n");
}

// 6. suptitle + auto layout — 2x2 grid, no axes titles anywhere
static void test_suptitle_layout() {
    constexpr int N = 60;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }

    auto fig = sextant::Figure::create({.width = 900, .height = 700, .title = "suptitle + layout"});
    fig->suptitle("Figure-wide title");
    auto ax1 = fig->add_subplot(2, 2, 1);
    auto ax2 = fig->add_subplot(2, 2, 2);
    auto ax3 = fig->add_subplot(2, 2, 3);
    auto ax4 = fig->add_subplot(2, 2, 4);
    ax1->line(x, y, {.color = sextant::Color::Blue});
    ax2->line(x, y, {.color = sextant::Color::Red});
    ax3->line(x, y, {.color = sextant::Color::Green});
    ax4->line(x, y, {.color = sextant::Color::Purple});
    // No titles anywhere: the layout should reserve nothing for them.

    fig->savefig("test_suptitle_tight.png");
    // Fig->show();
    printf("Saved: test_suptitle_tight.png\n");
}

// 7. Widget panel + pan/zoom — right-side Dear ImGui control panel.
static void test_widget_panel() {
    constexpr int N = 100;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }
    const std::vector<double> months = {1, 2, 3, 4, 5, 6};
    const std::vector<double> sales = {42, 55, 61, 49, 78, 83};

    auto fig = sextant::Figure::create({.width = 1100, .height = 600, .title = "Widget panel"});
    fig->add_subplot(1, 2, 1)
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f})
            .set_title("Sine").set_xtitle("x").set_ytitle("y").grid();
    fig->add_subplot(1, 2, 2)
            ->bar(months, sales, {.color = sextant::Color::Cyan})
            .set_title("Sales");

    fig->show(true); // panel edits apply live via the window thread's own per-frame drain
}

// 12. Frame-driven resize: the window grows so the plot frame gets the
// requested size. Interactive; the headless half is in sextant_layout_test.
static void test_frame_resize() {
    constexpr int N = 120;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 4.0 * M_PI / N;
        y[i] = std::sin(x[i]) * std::exp(-x[i] / 12.0);
    }

    auto fig = sextant::Figure::create({.width = 700, .height = 500, .title = "Frame resize"});
    fig->axes()->line(x, y, {
                          .color = sextant::Color::Blue, .linewidth = 2.0f,
                          .name = "damped sine"
                      })
            .set_title("Damped sine").set_xtitle("t (s)").set_ytitle("amplitude")
            .legend().grid();

    const auto a = fig->size_for_frame(400, 300);
    printf("[main] a 400x300 plot frame needs a %dx%d figure\n", a.width, a.height);

    fig->show(false);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    printf("[main] resizing so the plot frame is 400x300\n");
    fig->resize_to_frame(400, 300);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    printf("[main] resizing so the plot frame is 700x260\n");
    fig->resize_to_frame(700, 260);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    printf("[main] press ENTER to close\n");
    std::cin.get();
}

// Subplot spans: one 3x3 grid with four spans and two single cells (one 3D):
//
//     +-------+-------+-------+
//     |       |   2   |  3 3D |
//     | {1,4} +-------+-------+
//     |       |     {5,6}     |
//     +-------+-------+-------+
//     |         {7,9}         |
//     +-------+-------+-------+
//
// Non-zero gaps, so spans visibly include them. Check (layout itself is
// tested in sextant_layout_test):
//   * span frames line up with their neighbours;
//   * clicking anywhere in a span (including inner gaps) selects and outlines
//     it; a gap between subplots selects nothing;
//   * the subplot combo lists spans by first cell (Axis 1, 5, 7);
//   * Navigate works on a span, and orbits cell 3, only while selected;
//   * hover hints everywhere, and the heatmap's colorbar beside {5,6};
//   * File > Resize to plot frame sizes the selected span's frame;
//   * the saved PNG/SVG match the window.
static void test_subplot_spans() {
    constexpr int N = 240;
    std::vector<double> t(N), s1(N), s2(N), drift(N);
    for (int i = 0; i < N; ++i) {
        t[i] = i * 6.0 * M_PI / N;
        s1[i] = std::sin(t[i]) * std::exp(-t[i] / 14.0);
        s2[i] = std::cos(t[i] * 0.5) * 0.6;
        drift[i] = 0.02 * i + std::sin(t[i] * 3.0) * 0.4;
    }

    std::mt19937 rng(7);
    std::normal_distribution<double> nd(0.0, 1.0);
    std::vector<double> sx(150), sy(150);
    for (std::size_t i = 0; i < sx.size(); ++i) {
        sx[i] = nd(rng);
        sy[i] = 0.6 * sx[i] + 0.5 * nd(rng);
    }

    constexpr int R = 30, C = 90;
    std::vector<float> field(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c)
            field[r * C + c] = static_cast<float>(
                0.5 + 0.5 * std::sin(c * 0.12) * std::cos(r * 0.25));

    constexpr int SU = 21, SV = 21;
    std::vector<double> su(SU), sv(SV), sz(SU * SV);
    for (int i = 0; i < SU; ++i) su[i] = -3.0 + 6.0 * i / (SU - 1);
    for (int j = 0; j < SV; ++j) sv[j] = -3.0 + 6.0 * j / (SV - 1);
    for (int i = 0; i < SU; ++i)
        for (int j = 0; j < SV; ++j)
            sz[static_cast<std::size_t>(i) * SV + j] = std::exp(-(su[i] * su[i] + sv[j] * sv[j]) / 4.0);

    auto fig = sextant::Figure::create({
        .width = 1200, .height = 820, .title = "Subplot spans",
        .subplot_col_gap = 18.0f, .subplot_row_gap = 14.0f
    });
    fig->suptitle("Subplot spans on one 3x3 grid");

    // The first call fixes the grid; every later one is shape-less.
    fig->add_subplot(3, 3, {1, 4})
            ->line(t, s1, {.color = sextant::Color::Blue, .linewidth = 2.0f, .name = "damped", .show_legend = false})
            .line(t, s2, {
                      .color = sextant::Color::Red, .linestyle = sextant::LineStyle::Dashed,
                      .name = "slow"
                  })
            .set_title("{1, 4}: two rows tall").set_xtitle("t").set_ytitle("signal")
            .legend().grid();

    fig->add_subplot(2)
            ->scatter(sx, sy, {.color = sextant::Color::Green, .name = "something", .alpha = 0.6f})
            .set_title("cell 2").set_xtitle("x").set_ytitle("y");

    auto a3 = fig->add_subplot3d(3);
    a3->set_title("cell 3 (3D)").set_xtitle("x").set_ytitle("y").set_ztitle("z");
    a3->surface(sextant::PlaneOrientation::XY, su, sv, sz,
                {.colormap = true, .cmap = sextant::Colormap::Viridis, .colorbar = true, .name = "test..."});

    fig->add_subplot({5, 6})
            ->heatmap(field, R, C, sextant::Range{0.0, 90.0}, sextant::Range{0.0, 30.0},
                      {.cmap = sextant::Colormap::Viridis, .colorbar = true})
            .set_title("{5, 6}: two columns wide").set_xtitle("column").set_ytitle("row");

    fig->add_subplot({7, 9})
            ->line(t, drift, {.color = sextant::Color::Orange, .linewidth = 1.5f, .name = "drift"})
            .set_title("{7, 9}: the whole bottom row").set_xtitle("t").set_ytitle("level")
            .legend().grid();

    // Each of these throws; print the messages.
    auto expect_throw = [](const char* what, auto&& fn) {
        try {
            fn();
            printf("[spans] %-26s did NOT throw\n", what);
        } catch (const std::invalid_argument& e) { printf("[spans] %-26s -> %s\n", what, e.what()); }
    };
    expect_throw("add_subplot(5)", [&] { fig->add_subplot(5); });
    expect_throw("add_subplot({8, 9})", [&] { fig->add_subplot({8, 9}); });
    expect_throw("add_subplot3d({5, 6})", [&] { fig->add_subplot3d({5, 6}); });
    expect_throw("add_subplot(2, 2, 1)", [&] { fig->add_subplot(2, 2, 1); });
    expect_throw("add_subplot({3, 4})", [&] { fig->add_subplot({3, 4}); });

    fig->savefig("test_subplot_spans.png");
    fig->savefig("test_subplot_spans.svg");
    printf("Saved: test_subplot_spans.png/.svg\n");

    fig->show(true);
}

static void test_theming() {
    constexpr int N = 100;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }
    const std::vector<double> months = {1, 2, 3, 4, 5, 6};
    const std::vector<double> sales = {42, 55, 61, 49, 78, 83};

    auto make_fig = [&](sextant::PanelTheme theme, std::string_view title) {
        auto fig = sextant::Figure::create({.width = 1100, .height = 600, .title = std::string(title), .theme = theme});
        fig->add_subplot(1, 2, 1)
                ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f})
                .set_title("Sine").set_xtitle("x").set_ytitle("y").grid();
        fig->add_subplot(1, 2, 2)
                ->bar(months, sales, {.color = sextant::Color::Cyan})
                .set_title("Sales");
        return fig;
    };
    // Named variables so all three windows stay open together.
    auto dark = make_fig(sextant::PanelTheme::Dark, "Theming: Dark");
    auto light = make_fig(sextant::PanelTheme::Light, "Theming: Light");
    auto classic = make_fig(sextant::PanelTheme::Classic, "Theming: Classic");
    dark->show(false);
    light->show(false);
    classic->show(true);
}

// 9. Mouse hint -- hover tooltips over a grid of kinds. Interactive; checklist
// in the test docs.
static void test_mouse_hint() {
    constexpr int N = 60;
    std::vector<double> x(N), y(N);
    std::vector<std::string> line_hints(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }
    // Label the max and min of the sine wave.
    const auto peak = std::max_element(y.begin(), y.end()) - y.begin();
    const auto trough = std::min_element(y.begin(), y.end()) - y.begin();
    line_hints[static_cast<std::size_t>(peak)] = "Peak";
    line_hints[static_cast<std::size_t>(trough)] = "Trough";

    constexpr int NZ = 40;
    std::vector<double> zx(NZ), zy(NZ), zz(NZ);
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (int i = 0; i < NZ; ++i) {
        zx[i] = dist(rng) * 10.0;
        zy[i] = dist(rng) * 10.0;
        zz[i] = dist(rng);
    }

    constexpr int ROWS = 8, COLS = 12;
    std::vector<float> img(ROWS * COLS);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            img[r * COLS + c] = static_cast<float>(r * COLS + c) / static_cast<float>(ROWS * COLS);

    // Every point here has its own label (only two do on the line above).
    const std::vector<double> store_x = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<double> store_y = {4, 7, 2, 9, 5, 3, 8, 6};
    const std::vector<std::string> store_hints = {
        "Store A", "Store B", "Store C", "Store D",
        "Store E", "Store F", "Store G", "Store H",
    };

    // The line also has error bars, so its tooltip combines both with a
    // custom label.
    std::vector<double> hint_lo(N), hint_hi(N), hint_box(N);
    for (int i = 0; i < N; ++i) {
        const auto j = static_cast<std::size_t>(i);
        hint_lo[j] = 0.05 + 0.002 * i;
        hint_hi[j] = 0.15 + 0.004 * i;
        hint_box[j] = 0.04;
    }

    // 3D cell: a plane cutting through a bar grid. The tooltip must switch
    // between plane cell and bar exactly where the picture's occlusion does.
    constexpr int BU = 6, BV = 6;
    std::vector<double> bx(BU), by(BV), bh(BU * BV);
    std::vector<std::string> bar_hints(BU * BV);
    for (int i = 0; i < BU; ++i) bx[i] = 1.0 + i;
    for (int j = 0; j < BV; ++j) by[j] = 1.0 + j;
    for (int i = 0; i < BU; ++i)
        for (int j = 0; j < BV; ++j) {
            const double a = (i - BU / 2.0) / 1.6, b = (j - BV / 2.0) / 1.6;
            bh[i * BV + j] = 5.0 * std::exp(-(a * a + b * b));
        }
    // Only a few labelled, to check the index.
    bar_hints[static_cast<std::size_t>(BU / 2 * BV + BV / 2)] = "Peak bar";
    bar_hints[0] = "Corner bar";

    constexpr int PR = 10, PC = 10;
    std::vector<float> sheet(PR * PC);
    for (int r = 0; r < PR; ++r)
        for (int c = 0; c < PC; ++c)
            sheet[r * PC + c] = static_cast<float>(r + c) / static_cast<float>(PR + PC);

    auto fig = sextant::Figure::create({.width = 2400, .height = 500, .title = "Mouse hint"});
    fig->add_subplot(1, 5, 1)
            ->line(x, y, {.y_cap_lo = hint_lo, .y_cap_hi = hint_hi, .y_box_lo = hint_box}, {
                       .color = sextant::Color::Blue, .linewidth = 2.0f,
                       .hint_labels = line_hints
                   })
            .set_title("Line").set_xtitle("x").set_ytitle("y");
    fig->add_subplot(1, 5, 2)
            ->scatter_z(zx, zy, zz, {.size = 30.0f, .colorbar = true})
            .set_title("Scatter Z");
    // A real extent (x 100..400, y -2..2), so the hint must name the cell
    // under the cursor, not the raw index.
    fig->add_subplot(1, 5, 3)
            ->heatmap(img, ROWS, COLS, {100.0, 400.0}, {-2.0, 2.0}, {.colorbar = true})
            .set_title("Heatmap (x 100..400, y -2..2)");
    fig->add_subplot(1, 5, 4)
            ->scatter(store_x, store_y, {.color = sextant::Color::Orange, .size = 24.0f, .hint_labels = store_hints})
            .set_title("Custom hint").set_xlim(0, 9).set_ylim(0, 10); {
        auto ax3 = fig->add_subplot3d(1, 5, 5);
        ax3->bar3d(sextant::PlaneOrientation::XY, bx, by, bh,
                   {
                       .color = sextant::Color::Blue, .bottom = 0.0,
                       .edges = true, .edgecolor = sextant::Color::Black,
                       .hint_labels = bar_hints
                   })
                .set_title("bar3d + a plane through it")
                .set_xtitle("u").set_ytitle("v").set_ztitle("height");
        // Vertical, standing among the bars (near/far switch visible).
        ax3->plane(sextant::PlaneOrientation::YZ, 3.5, {.alpha = 0.85f})
                ->heatmap(sheet, PR, PC, {0.0, 7.0}, {0.0, 5.5});
    }
    fig->show(true);
}

// 10. Data panel -- editable tables. Interactive; checklist in the test docs.
static void test_data_panel() {
    constexpr int N = 400; // enough rows to make clipper virtualization visible
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }

    constexpr int NZ = 30;
    std::vector<double> zx(NZ), zy(NZ), zz(NZ);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (int i = 0; i < NZ; ++i) {
        zx[i] = dist(rng) * 10.0;
        zy[i] = dist(rng) * 10.0;
        zz[i] = dist(rng);
    }

    std::vector<double> samples(300);
    std::normal_distribution<double> norm(0.0, 1.0);
    for (auto& s: samples) s = norm(rng);

    // Samples on the same axes as the sine line: both are plot_index 0 in their
    // own kind, so PlotKind distinguishes their edits.
    constexpr int NP = 12;
    std::vector<double> px(NP), py(NP);
    for (int i = 0; i < NP; ++i) {
        px[i] = i * 2.0 * M_PI / NP;
        py[i] = std::sin(px[i]) + 0.15 * norm(rng);
    }

    constexpr int ROWS = 8, COLS = 6;
    std::vector<float> img(ROWS * COLS);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            img[r * COLS + c] = static_cast<float>(r * COLS + c) / static_cast<float>(ROWS * COLS);

    auto fig = sextant::Figure::create({.width = 1400, .height = 800, .title = "Data panel"});
    fig->add_subplot(2, 2, 1)
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f, .name = "Sine"})
            .scatter(px, py, {.color = sextant::Color::Red, .size = 24.0f, .name = "Samples"})
            .set_title("Line + scatter").set_xtitle("x").set_ytitle("y").legend();
    fig->add_subplot(2, 2, 2)
            ->scatter_z(zx, zy, zz, {.size = 30.0f, .colorbar = true})
            .set_title("Scatter Z");
    fig->add_subplot(2, 2, 3)
            ->hist(samples, 12, {.color = sextant::Color::Orange, .width = 1.0f})
            .set_title("Hist (a BarPlot)");
    fig->add_subplot(2, 2, 4)
            ->imshow(img, ROWS, COLS, {.colorbar = true})
            .set_title("Heatmap 8x6");
    fig->show(false);

    // Wider than one table: exercises column paging. Both Data panels are open
    // at once.
    constexpr int WROWS = 12, WCOLS = 600;
    std::vector<float> wide(WROWS * WCOLS);
    for (int r = 0; r < WROWS; ++r)
        for (int c = 0; c < WCOLS; ++c)
            wide[r * WCOLS + c] = static_cast<float>(c) / static_cast<float>(WCOLS);

    auto fig2 = sextant::Figure::create({.width = 1400, .height = 700, .title = "Data panel: wide heatmap"});
    fig2->axes()->imshow(wide, WROWS, WCOLS, {.colorbar = true}).set_title("Heatmap 12x600");
    fig2->show(false);

    // A bar3d grid (u/v headers, addressed at the axes, with per-bar bases so
    // the heights/bases selector is live) beside a plane.
    constexpr int BU = 7, BV = 5;
    std::vector<double> bx(BU), by(BV), bh(BU * BV), bb(BU * BV);
    for (int i = 0; i < BU; ++i) bx[i] = 400.0 + i * 25.0; // wavelength, nm
    for (int j = 0; j < BV; ++j) by[j] = -1.0 + j * 0.5;
    for (int i = 0; i < BU; ++i)
        for (int j = 0; j < BV; ++j) {
            const double a = (i - BU / 2.0) / 2.0, b = (j - BV / 2.0) / 1.6;
            bh[i * BV + j] = 12.0 * std::exp(-(a * a + b * b));
            bb[i * BV + j] = 0.5 * i; // a staircase, so the bases are visibly per bar
        }

    constexpr int PR = 6, PC = 8;
    std::vector<float> sheet(PR * PC);
    for (int r = 0; r < PR; ++r)
        for (int c = 0; c < PC; ++c)
            sheet[r * PC + c] = static_cast<float>(r * PC + c) / static_cast<float>(PR * PC);

    auto fig3 = sextant::Figure::create({.width = 1400, .height = 800, .title = "Data panel: bar3d"});
    auto ax3 = fig3->add_subplot3d(1, 1, 1);
    ax3->bar3d(sextant::PlaneOrientation::XY, bx, by, bh, bb,
               {
                   .color = sextant::Color::Blue,
                   .edges = true, .edgecolor = sextant::Color::Black
               })
            .set_title("bar3d 7x5, per-bar bases")
            .set_xtitle("wavelength (nm)").set_ytitle("offset").set_ztitle("counts");
    ax3->plane(sextant::PlaneOrientation::ZX, -1.25)
            ->heatmap(sheet, PR, PC, {0.0, 14.0}, {390.0, 560.0});
    fig3->show(true);
}

// Every cosmetic option rendered all-default and all-non-default, to show each
// reaches both outputs. Headless.
static void test_style_options() {
    constexpr int N = 60;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y[i] = std::sin(x[i]);
    }
    constexpr int R = 16, C = 16;
    std::vector<float> heat(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c)
            heat[r * C + c] = static_cast<float>(r + c) / static_cast<float>(R + C - 2);

    auto render = [&](bool styled, const std::string& stem) {
        // Supersample=1, so the raster output isn't box-filtered.
        auto fig = sextant::Figure::create({
            .width = 900, .height = 500,
            .title = "Style options", .supersample = 1
        });
        fig->suptitle("Suptitle text");

        auto ax = fig->add_subplot(1, 2, 1);
        ax->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f, .name = "sin(x)"})
                .set_title("Titled").set_xtitle("x axis").set_ytitle("y axis")
                .legend().grid();

        auto ax2 = fig->add_subplot(1, 2, 2);
        ax2->imshow(heat, R, C, {.vmin = 0.0f, .vmax = 1.0f, .colorbar = true})
                .set_title("Heat");

        if (styled) {
            sextant::SuptitleOptions sup;
            sup.fontsize = 28.0f;
            sup.color = {0.0f, 0.2f, 0.9f, 1.0f}; // blue
            sup.align = sextant::HAlign::Right;
            sup.offset_x = -20.0f;
            sup.offset_y = 4.0f;
            fig->set_suptitle_style(sup);

            // Also carries the title font sizes (set_axes_style replaces the
            // whole struct).
            sextant::AxesStyle as;
            as.spine_color = {0.9f, 0.1f, 0.1f, 1.0f};
            as.spine_linewidth = 3.0f;
            as.tick_color = {0.1f, 0.6f, 0.1f, 1.0f};
            as.tick_length = 12.0f;
            as.label_color = {0.9f, 0.0f, 0.0f, 1.0f}; // per-tick labels
            as.label_fontsize = 17.0f;
            as.title_fontsize = 30.0f;
            as.xtitle_fontsize = 24.0f;
            as.ytitle_fontsize = 24.0f;
            ax->set_axes_style(as);

            ax->grid(true, {
                         .color = {0.0f, 0.6f, 0.6f, 1.0f}, // teal
                         .linestyle = sextant::LineStyle::Dashed,
                         .linewidth = 2.0f
                     });

            sextant::LegendOptions lo;
            lo.fontsize = 16.0f;
            lo.text_color = {0.0f, 0.5f, 0.0f, 1.0f}; // green
            lo.frame_color = {0.1f, 0.1f, 0.1f, 0.9f};
            lo.border_color = {1.0f, 0.5f, 0.0f, 1.0f};
            lo.border_linewidth = 3.0f;
            ax->legend(lo);

            sextant::ColorbarOptions cb;
            cb.fontsize = 16.0f;
            cb.text_color = {1.0f, 0.4f, 0.0f, 1.0f}; // orange
            cb.border_color = {0.5f, 0.0f, 0.5f, 1.0f}; // purple
            cb.border_linewidth = 3.0f;
            ax2->set_colorbar_style(cb);
        }

        fig->savefig(stem + ".png");
        fig->savefig(stem + ".svg");
    };

    render(false, "test_style_default");
    render(true, "test_style_custom");
    printf("Saved: test_style_{default,custom}.{png,svg}\n");
}

// Axis position in 2D: placements, origin components and spine flags.
static void test_axis_position() {
    constexpr int N = 200;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = -4.0 + 8.0 * i / (N - 1);
        y[i] = std::sin(x[i]) * std::exp(-std::abs(x[i]) / 5.0);
    }
    // Data away from zero, for the cell that pins the origin.
    std::vector<double> fx(N), fy(N);
    for (int i = 0; i < N; ++i) {
        fx[i] = 5.0 + 5.0 * i / (N - 1);
        fy[i] = 20.0 + 6.0 * std::sin(fx[i] * 1.5);
    }

    auto fig = sextant::Figure::create({
        .width = 1200, .height = 800,
        .title = "Axis position (2D)",
        .subplot_col_gap = 26.0f, .subplot_row_gap = 20.0f
    });
    fig->suptitle("AxesStyle: axis position and spines");

    // 1 — the default, as reference.
    fig->add_subplot(2, 2, 1)
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f})
            .set_title("default (Auto = bottom/left)").set_xtitle("x").set_ytitle("y").grid();

    // 2 — High on both: bands, ticks and numbers flip; titles stay.
    {
        sextant::AxesStyle st;
        st.xaxis_y = sextant::AxisPosition::High;
        st.yaxis_x = sextant::AxisPosition::High;
        fig->add_subplot(2)
                ->line(x, y, {.color = sextant::Color::Red, .linewidth = 2.0f})
                .set_axes_style(st)
                .set_title("High / High -- bands swap sides")
                .set_xtitle("x").set_ytitle("y").grid();
    }

    // 3 — the crosshair: both axes through zero, every spine off.
    {
        sextant::AxesStyle st;
        st.origin_x = 0.0;
        st.origin_y = 0.0;
        st.spine_bottom = st.spine_left = st.spine_top = st.spine_right = false;
        fig->add_subplot(3)
                ->line(x, y, {.color = sextant::Color::Purple, .linewidth = 2.0f})
                .set_axes_style(st)
                .set_title("origin (0,0), no box")
                .set_xtitle("x").set_ytitle("y");
    }

    // 4 — the same pin over data far from zero: the limits widen to show it.
    {
        sextant::AxesStyle st;
        st.origin_x = 0.0;
        st.origin_y = 0.0;
        st.spine_top = st.spine_right = false;
        fig->add_subplot(4)
                ->line(fx, fy, {.color = sextant::Color::Orange, .linewidth = 2.0f})
                .set_axes_style(st)
                .set_title("origin (0,0) with data at x 5..10")
                .set_xtitle("x").set_ytitle("y").grid();
    }

    fig->savefig("test_axis_position.png");
    fig->savefig("test_axis_position.svg");
    printf("Saved: test_axis_position.png/.svg\n");

    fig->show(true);
}

// -------------------------------------------------------------------------
// Axis position in 3D. As in 2D, plus: an Auto edge turns with the camera and
// a Low/High one doesn't (orbit to see).
static void test_axis_position3d() {
    constexpr int HN = 120;
    std::vector<double> hx(HN), hy(HN), hz(HN);
    for (int i = 0; i < HN; ++i) {
        const double t = i * 6.2831853 * 2.0 / (HN - 1);
        hx[i] = 2.2 * std::cos(t);
        hy[i] = 2.2 * std::sin(t);
        hz[i] = -2.5 + 5.0 * i / (HN - 1);
    }
    // A cloud far from the origin, for the cell whose pin widens the box.
    std::vector<double> fx, fy, fz;
    for (int i = 0; i < 60; ++i) {
        const double t = i * 0.5;
        fx.push_back(6.0 + 1.5 * std::sin(t));
        fy.push_back(7.0 + 1.5 * std::cos(t * 1.3));
        fz.push_back(5.0 + 2.0 * std::sin(t * 0.7));
    }

    auto fig = sextant::Figure::create({
        .width = 1280, .height = 860,
        .title = "Axis position (3D)",
        .subplot_col_gap = 24.0f, .subplot_row_gap = 20.0f
    });
    fig->suptitle("AxesStyle: axis position on the 3D box");

    // 1 — the reference: every axis on its silhouette edge.
    fig->add_subplot3d(2, 2, 1)
            ->line3d(hx, hy, hz, {.color = sextant::Color::Blue, .linewidth = 2.0f})
            .set_title("default (Auto = camera's edge)")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .set_view(-55.0, 22.0);

    // 2 — the crosshair: all three components at the data's middle. Panes,
    // grid and pane edges don't move.
    {
        sextant::AxesStyle st;
        st.origin_x = 0.0;
        st.origin_y = 0.0;
        st.origin_z = 0.0;
        fig->add_subplot3d(2, 2, 2)
                ->line3d(hx, hy, hz, {.color = sextant::Color::Purple, .linewidth = 2.0f})
                .set_axes_style(st)
                .set_title("origin (0,0,0) -- a crosshair in the box")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-55.0, 22.0);
    }

    // 3 — origin_z only: the x and y axes drop to z = 0; the z axis is pinned
    // High (orbit to check it doesn't migrate).
    {
        sextant::AxesStyle st;
        st.origin_z = 0.0;
        st.zaxis_x = sextant::AxisPosition::High;
        st.zaxis_y = sextant::AxisPosition::High;
        fig->add_subplot3d(2, 2, 3)
                ->line3d(hx, hy, hz, {.color = sextant::Color::Green, .linewidth = 2.0f})
                .set_axes_style(st)
                .set_title("origin_z only; z axis pinned High/High")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-55.0, 22.0);
    }

    // 4 — the pin over data far from it: the limits widen to reach zero.
    {
        sextant::AxesStyle st;
        st.origin_x = 0.0;
        st.origin_y = 0.0;
        st.origin_z = 0.0;
        fig->add_subplot3d(2, 2, 4)
                ->scatter3d(fx, fy, fz, {.color = sextant::Color::Orange, .size = 6.0f})
                .set_axes_style(st)
                .set_title("origin (0,0,0) with data around (6,7,5)")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_view(-55.0, 22.0);
    }

    fig->savefig("test_axis_position3d.png");
    fig->savefig("test_axis_position3d.svg");
    printf("Saved: test_axis_position3d.png/.svg\n");

    fig->show(true);
}

// -------------------------------------------------------------------------
int main() {
    test_axis_position3d();
    // test_axis_position();
    // test_axes_gallery();
    // test_axes3d_gallery();
    // test_errorbar3d_gallery();
    // test_line3d_gallery();
    // test_surface_tri_gallery();
    // test_subplot_spans();
    // test_style_options();
    // test_suptitle_layout();
    // test_savefig();
    // test_translucent3d();
    // test_show_nonblocking();
    // test_refresh_before_show();
    // test_live_refresh();
    // test_grid_toggle();
    // test_widget_panel();
    // test_theming();
    // test_frame_resize();
    // test_mouse_hint();
    // test_data_panel();
}
