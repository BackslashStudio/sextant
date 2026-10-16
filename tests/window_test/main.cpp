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

// Sleep for `total` while the process's windows stay pumped. poll_events() is a
// no-op on Windows and Linux, where each window thread polls its own events; a
// wait written this way is already right where the events belong to the main
// thread. Every wait below a live window uses it rather than a plain sleep.
static void pump_for(std::chrono::milliseconds total) {
    const auto deadline = std::chrono::steady_clock::now() + total;
    while (std::chrono::steady_clock::now() < deadline) {
        sextant::Figure::poll_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

static std::string slurp(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
    std::fclose(f);
    return s;
}

// Save `stem`.png/.svg, then check the in-memory export is those files byte for
// byte. The PNG is rendered twice, so on a renderer that does
// not repeat an export exactly (Apple's software one) it may differ.
static void savefig_and_check_memory(const std::shared_ptr<sextant::Figure>& fig,
                                     const std::string& stem) {
    fig->savefig(stem + ".png");
    fig->savefig(stem + ".svg");

    const std::vector<std::uint8_t> png = fig->render_png();
    const bool png_same = slurp(stem + ".png") == std::string(png.begin(), png.end());
    const bool svg_same = slurp(stem + ".svg") == fig->render_svg().svg;
    const sextant::RgbaImage rgba = fig->render_rgba();
    const bool rgba_ok = rgba.pixels.size() == std::size_t(rgba.width) * rgba.height * 4;
    printf("Saved: %s.png, %s.svg -- memory export: png %s, svg %s, rgba %dx%d %s\n",
           stem.c_str(), stem.c_str(), png_same ? "identical" : "DIFFERS",
           svg_same ? "identical" : "DIFFERS", rgba.width, rgba.height,
           rgba_ok ? "ok" : "BAD SIZE");
}

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
    std::vector<double> gauss(R * C), checker(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            double dr = (r - R / 2.0) / (R / 6.0), dc = (c - C / 2.0) / (C / 6.0);
            gauss[r * C + c] = std::exp(-(dr * dr + dc * dc));
            checker[r * C + c] = static_cast<double>((r + c) % 2);
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
        .title = "Axes gallery", .background = sextant::Color::White, .supersample = 2
    });
    fig->suptitle("What Axes draws");

    fig->add_subplot(3, 4, 1)
            // Math in text: the legend names, the annotation, the x and y titles.
            ->line(x, y_sin, {.color = sextant::Color::Blue, .linewidth = 4.0f, .name = "$\\sin(x)$"})
            .line(x, y_cos, {
                      .color = sextant::Color::Red, .linewidth = 2.0f,
                      .linestyle = sextant::LineStyle::Dashed, .name = "$\\cos(x)$"
                  })
            .line(pent_x, pent_y, {
                      .color = sextant::Color::Green, .linewidth = 3.0f,
                      .name = "loop", .loop = true
                  })
            // Text: an annotation whose text sits in the frame, not the data.
            .annotate(M_PI / 2.0, 1.0, "$\\sin(\\pi/2) = 1$", sextant::Pos::fraction(0.55),
                      sextant::Pos::fraction(0.93),
                      {.va = sextant::VAlign::Top, .background = {1.0f, 1.0f, 0.85f, 1.0f}, .edge_linewidth = 1.0f})
            .set_xtitle("$\\hat{\\theta}$ [rad]").set_ytitle("$\\frac{\\sqrt{2}}{2}\\,\\overline{y}$")
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
    // numbers, in two different colormaps (Gray field, Plasma markers).
    fig->add_subplot(3, 4, 12)
            ->imshow(gauss, R, C, {
                         .cmap = sextant::Colormap::Gray, .vmin = 0.0f, .vmax = 1.0f,
                         .colorbar = true, .name = "field"
                     })
            .scatter_z(ring_x, ring_y, ring_z, {
                           .cmap = sextant::Colormap::Plasma, .size = 22.0f, .marker = sextant::MarkerStyle::Square,
                           .vmin = 0.0f, .vmax = static_cast<float>(2.0 * M_PI),
                           .colorbar = true, .name = "phase"
                       })
            .set_title("Two colorbars").legend();

    savefig_and_check_memory(fig, "test_axes_gallery");
    fig->show(true);
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
    std::vector<double> field(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            const double dr = (r - R / 2.0) / (R / 3.0), dc = (c - C / 2.0) / (C / 3.0);
            field[r * C + c] = std::exp(-(dr * dr + dc * dc));
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
    auto bars_ax = fig->add_subplot3d(2, 4, 1);
    bars_ax->bar3d(sextant::PlaneOrientation::XY, gu, gv, gauss,
                   {.color = sextant::Color::Blue, .name = "counts"})
            .set_title("bar3d")
            .text2d("text2d: stays put", 0.02, 0.98, {.va = sextant::VAlign::Top})
            .set_xtitle("wavelength (nm)").set_ytitle("offset").set_ztitle("counts")
            .set_view(-55.0, 25.0)
            .legend();
    // A 3D annotation on the tallest bar's top: its arrow and text keep their
    // pixel size as the box turns and zooms.
    {
        const auto top = std::max_element(gauss.begin(), gauss.end()) - gauss.begin();
        const std::size_t ti = static_cast<std::size_t>(top) / gv.size();
        const std::size_t tj = static_cast<std::size_t>(top) % gv.size();
        bars_ax->annotate(gu[ti], gv[tj], gauss[static_cast<std::size_t>(top)], "peak", 50.0, 40.0,
                      {.background = {1.0f, 1.0f, 0.85f, 1.0f}, .edge_linewidth = 1.0f});
    }

    // 2 — surface colored by height through the diverging Coolwarm, with a
    // named colorbar.
    fig->add_subplot3d(2, 4, 2)
            ->surface(sextant::PlaneOrientation::XY, su, sv, ripple,
                      {
                          .colormap = true, .cmap = sextant::Colormap::Coolwarm,
                          .colorbar = true, .name = "amplitude",
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

    // 4 — Plane2D carrying Inferno heatmaps; the colorbar is beside the cell.
    {
        auto ax = fig->add_subplot3d(2, 4, 4);
        ax->set_title("Plane2D — slices")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z")
                .set_zlim(0.0, 3.0)
                .set_view(-60.0, 20.0);
        ax->plane(sextant::PlaneOrientation::XY, 0.2)
                ->heatmap(field, R, C, {0.0, 6.0}, {0.0, 6.0},
                          {.cmap = sextant::Colormap::Inferno, .vmin = 0.0f, .vmax = 1.0f,
                           .colorbar = true, .name = "intensity"});
        // Two more translucent slices, stacked.
        for (double z: {1.4, 2.6})
            ax->plane(sextant::PlaneOrientation::XY, z, {.alpha = 0.55f})
                    ->heatmap(field, R, C, {0.0, 6.0}, {0.0, 6.0},
                              {.cmap = sextant::Colormap::Inferno, .vmin = 0.0f, .vmax = 1.0f});
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
                          .cmap = sextant::Colormap::Cividis, .colorbar = true, .name = "energy"
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
    savefig_and_check_memory(fig, "test_axes3d_gallery");
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
    savefig_and_check_memory(fig, "test_line3d_gallery");
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
    savefig_and_check_memory(fig, "test_errorbar3d_gallery");
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
                        {.cmap = sextant::Colormap::Turbo, .colorbar = true, .name = "height",
                         .shading = 0.35f});
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
                        {.cmap = sextant::Colormap::Magma, .colorbar = true, .name = "x + y",
                         .shading = 0.85f});
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
    savefig_and_check_memory(fig, "test_surface_tri_gallery");
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
        std::vector<double> m(static_cast<std::size_t>(NY) * NX);
        for (int r = 0; r < NY; ++r)
            for (int c = 0; c < NX; ++c) {
                const double x = -3.0 + 6.0 * c / (NX - 1);
                const double y = -3.0 + 6.0 * r / (NY - 1);
                m[static_cast<std::size_t>(r) * NX + c] =
                        std::sin(x + z) * std::cos(y - z);
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
    std::vector<double> gauss(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            double dr = (r - R / 2.0) / (R / 6.0), dc = (c - C / 2.0) / (C / 6.0);
            gauss[r * C + c] = std::exp(-(dr * dr + dc * dc));
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
        pump_for(std::chrono::milliseconds(600));
        printf("[main] tick %d  is_open=%s\n", i, fig->is_open() ? "true" : "false");
    }

    fig->close();
    printf("[main] close() called  is_open=%s\n", fig->is_open() ? "true" : "false");
}

// 3b. wait_closed() and run() — waiting on the windows themselves, with no
// console in it. Close the windows with their own buttons.
static void test_wait_and_run() {
    auto make = [](const char* title, double phase) {
        constexpr int N = 100;
        std::vector<double> x(N), y(N);
        for (int i = 0; i < N; ++i) {
            x[i] = i * 2.0 * M_PI / N;
            y[i] = std::sin(x[i] + phase);
        }
        auto fig = sextant::Figure::create({.width = 600, .height = 400, .title = title});
        fig->axes()->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f})
                .set_title(title).grid();
        return fig;
    };

    auto fig = make("wait_closed", 0.0);
    fig->show(false);
    printf("[main] close the window; wait_closed(2.0) reports every 2 s until you do\n");
    while (!fig->wait_closed(2.0)) printf("[main] still open\n");
    printf("[main] wait_closed() returned  is_open=%s\n", fig->is_open() ? "true" : "false");

    auto a = make("run A", 0.6), b = make("run B", 1.2);
    a->show(false);
    b->show(false);
    printf("[main] two windows open; run() returns when the last one closes\n");
    sextant::Figure::run();
    printf("[main] run() returned  A open=%s  B open=%s\n",
           a->is_open() ? "true" : "false", b->is_open() ? "true" : "false");
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

    // set_line_data() keeps the title and any pan/zoom, which cla() would reset.
    for (int i = 0; i < 20; ++i) {
        pump_for(std::chrono::milliseconds(50));
        std::vector<double> y(N);
        for (int j = 0; j < N; ++j) y[j] = std::sin(x[j] + i * 0.1);
        ax->set_line_data(0, {x, y});
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
    pump_for(std::chrono::seconds(3));

    printf("[main] resizing so the plot frame is 400x300\n");
    fig->resize_to_frame(400, 300);
    pump_for(std::chrono::seconds(3));

    printf("[main] resizing so the plot frame is 700x260\n");
    fig->resize_to_frame(700, 260);
    pump_for(std::chrono::seconds(3));

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
    std::vector<double> field(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c)
            field[r * C + c] = 0.5 + 0.5 * std::sin(c * 0.12) * std::cos(r * 0.25);

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
    std::vector<double> img(ROWS * COLS);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            img[r * COLS + c] = static_cast<double>(r * COLS + c) / static_cast<double>(ROWS * COLS);

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
    std::vector<double> sheet(PR * PC);
    for (int r = 0; r < PR; ++r)
        for (int c = 0; c < PC; ++c)
            sheet[r * PC + c] = static_cast<double>(r + c) / static_cast<double>(PR + PC);

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
    std::vector<double> img(ROWS * COLS);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            img[r * COLS + c] = static_cast<double>(r * COLS + c) / static_cast<double>(ROWS * COLS);

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
    std::vector<double> wide(WROWS * WCOLS);
    for (int r = 0; r < WROWS; ++r)
        for (int c = 0; c < WCOLS; ++c)
            wide[r * WCOLS + c] = static_cast<double>(c) / static_cast<double>(WCOLS);

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
    std::vector<double> sheet(PR * PC);
    for (int r = 0; r < PR; ++r)
        for (int c = 0; c < PC; ++c)
            sheet[r * PC + c] = static_cast<double>(r * PC + c) / static_cast<double>(PR * PC);

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
    std::vector<double> heat(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c)
            heat[r * C + c] = static_cast<double>(r + c) / static_cast<double>(R + C - 2);

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
// Interactive events. Click, scroll, press keys and resize the window;
// each event is printed on this thread (the one blocked in wait_closed()). The
// left figure is 2D, the right one a 3D scene with a Plane2D; Edit > Navigate
// shows `consumed`, and a click on a line point, bar, marker or heatmap cell adds a
// `pick` line naming the object and element. Type in a panel field to see keys
// stay quiet. Close the
// window to finish.
static void test_events() {
    const char* consumed_names[] = {"none", "select", "navigate", "grid-drag"};
    const char* kinds[] = {"close", "down", "up", "move", "scroll", "key-down", "key-up", "resize", "pick"};
    const char* pick_names[] = {"none", "line", "scatter", "scatter_z", "bar", "heatmap",
                                "bar3d", "surface", "surface_tri", "scatter3d", "line3d"};

    auto fig = sextant::Figure::create({.width = 900, .height = 450, .title = "events"});
    constexpr int N = 60;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 0.1;
        y[i] = std::sin(x[i]);
    }
    fig->add_subplot(1, 2, 1)->line(x, y).set_title("2D: click for data coordinates");
    auto ax3 = fig->add_subplot3d(1, 2, 2);
    ax3->set_title("3D: data only where a plane is hit");
    ax3->plane(sextant::PlaneOrientation::XY, 0.0)->line(x, y);

    int moves = 0;
    for (int k = 0; k <= static_cast<int>(sextant::EventKind::Pick); ++k) {
        const auto kind = static_cast<sextant::EventKind>(k);
        fig->connect(kind, [&, kind](const sextant::Event& e) {
            if (kind == sextant::EventKind::MouseMove && ++moves % 20 != 0) return; // thin the log
            printf("[event] %-8s", kinds[static_cast<int>(kind)]);
            switch (kind) {
                case sextant::EventKind::MouseDown:
                case sextant::EventKind::MouseUp:
                case sextant::EventKind::MouseMove:
                case sextant::EventKind::Scroll:
                    printf(" px=(%.0f,%.0f) axes=%d", e.x, e.y, e.axes);
                    if (e.has_data) printf(" data=(%.4g,%.4g,%.4g)", e.xdata, e.ydata, e.zdata);
                    if (kind == sextant::EventKind::Scroll) printf(" scroll=(%.2g,%.2g)", e.scroll_x, e.scroll_y);
                    if (kind != sextant::EventKind::MouseMove) printf(" button=%d dbl=%d", e.button, e.double_click);
                    printf(" consumed=%s mods=%d", consumed_names[static_cast<int>(e.consumed)], e.mods);
                    break;
                case sextant::EventKind::KeyDown:
                case sextant::EventKind::KeyUp:
                    printf(" key='%s'", e.key.c_str());
                    break;
                case sextant::EventKind::Resize:
                    printf(" %dx%d", e.width, e.height);
                    break;
                case sextant::EventKind::Pick:
                    printf(" %s object=%d index=%d row=%d col=%d plane=%d axes=%d button=%d",
                           pick_names[static_cast<int>(e.pick_kind)], e.pick_object, e.pick_index,
                           e.pick_row, e.pick_col, e.pick_plane, e.axes, e.button);
                    break;
                default: break;
            }
            printf("\n");
        });
    }
    fig->show(false);
    printf("[main] events are delivered inside wait_closed(); close the window to finish\n");
    fig->wait_closed();
    printf("[main] done\n");
}

// -------------------------------------------------------------------------
// Axis scales (v1.2 step 36a): log, log-log, symlog, minor ticks and grid, tick
// formats. Interactive: pan and zoom move whole decades on a log axis and keep
// the point under the cursor; the hover readout shows data values; the
// Cosmetic panel's Ticks section switches each axis' scale (disabled on the
// heatmap cell), its minor marks and its format (red while not a %f/%e/%g).
static void test_scales() {
    constexpr int N = 200;
    std::vector<double> t(N), growth(N), decay(N);
    for (int i = 0; i < N; ++i) {
        t[i] = 10.0 * i / (N - 1);
        growth[i] = std::exp(t[i]);
        decay[i] = 1e4 * std::exp(-1.5 * t[i]);
    }
    std::vector<double> f(N), mag(N);
    for (int i = 0; i < N; ++i) {
        f[i] = std::pow(10.0, -1.0 + 5.0 * i / (N - 1));
        mag[i] = 100.0 / std::sqrt(1.0 + std::pow(f[i] / 100.0, 4.0));   // 2nd-order low-pass, in %
    }
    std::vector<double> px, p1, p2, pm, perr;
    for (int i = 0; i <= 12; ++i) {
        const double v = std::pow(10.0, -1.0 + 4.0 * i / 12.0);
        px.push_back(v);
        p1.push_back(v);
        p2.push_back(v * v);
        pm.push_back(1.0 / v);
        perr.push_back(0.3 * v * v);
    }
    std::vector<double> s(N), sy(N);
    for (int i = 0; i < N; ++i) {
        s[i] = -10.0 + 20.0 * i / (N - 1);
        sy[i] = std::sinh(s[i]);
    }
    const std::vector<double> cells{1, 2, 3, 4, 5, 6};
    const std::vector<double> bins{1, 2, 3, 4, 5}, counts{3, 40, 700, 9000, 120000};
    const std::vector<double> count_err{1, 10, 100, 1000, 30000};

    auto fig = sextant::Figure::create({.width = 1400, .height = 850, .title = "Axis scales"});
    fig->suptitle("set_xscale / set_yscale, minor ticks, tick formats");

    fig->add_subplot(2, 3, 1)
            ->line(t, growth, {.name = "e^t"})
            .line(t, decay, {.name = "1e4 e^{-1.5t}"})
            .set_yscale(sextant::Scale::Log)
            .grid(true, {.minor = true})
            .legend()
            .set_title("semilog y, minor grid").set_xtitle("t").set_ytitle("value");

    fig->add_subplot(2, 3, 2)
            ->scatter(px, p1, {.name = "x"})
            .line(px, p2, {.y_cap_lo = perr}, {.name = "x^2 +- 30%"})
            .scatter(px, pm, {.name = "1/x"})
            .set_xscale(sextant::Scale::Log)
            .set_yscale(sextant::Scale::Log)
            .grid()
            .legend()
            .set_title("log-log, error bars").set_xtitle("x").set_ytitle("y");

    fig->add_subplot(2, 3, 3)
            ->line(f, mag, {.linewidth = 2.0f})
            .set_xscale(sextant::Scale::Log)
            .set_ytick_format("%.0f%%")
            .set_ylim(0.0, 110.0)
            .grid(true, {.minor = true})
            .set_title("semilog x, y as %.0f%%").set_xtitle("frequency (Hz)").set_ytitle("gain");

    fig->add_subplot(2, 3, 4)
            ->line(s, sy, {.linewidth = 2.0f})
            .set_yscale(sextant::Scale::Symlog, {.linthresh = 1.0})
            .grid()
            .set_title("symlog y: sinh(x)").set_xtitle("x").set_ytitle("sinh(x)");

    fig->add_subplot(2, 3, 5)
            ->bar(bins, counts, {.y_cap_lo = count_err}, {})
            .set_yscale(sextant::Scale::Log, {.base = 2.0})
            .set_xtick_format("#%.0f")
            .set_title("bars on log2 y (stand on the frame)").set_xtitle("bin").set_ytitle("count");

    fig->add_subplot(2, 3, 6)
            ->imshow(cells, 2, 3, {.vmin = 1.0f, .vmax = 6.0f})
            .set_title("heatmap: linear only (scale combo disabled)");

    savefig_and_check_memory(fig, "test_scales");
    fig->show(true);
}

// -------------------------------------------------------------------------
// Shared axes (v1.2 step 36b): sharex down the left column, sharey across the
// top row, and a heatmap sharing x below. Interactive: pan or zoom one cell of
// a group and the others follow on that axis only; the inner cells' tick labels
// are hidden (their room given back), the bottom row's shown; cell 1's legend
// and cell 5's colorbar narrow all three frames of the column alike; the Cosmetic
// panel says "X shared with ..." under Limits, its Labels X/Y combos override
// the hiding, and a scale set on one member is the group's (disabled on the
// heatmap's group).
static void test_shared_axes() {
    constexpr int N = 300;
    std::vector<double> t(N), a(N), b(N), c(N), d(N);
    for (int i = 0; i < N; ++i) {
        t[i] = 20.0 * i / (N - 1);
        a[i] = std::sin(t[i]);
        b[i] = 3.0 * std::cos(0.5 * t[i]) + 2.0;
        c[i] = std::exp(-0.1 * t[i]) * std::sin(3.0 * t[i]);
        d[i] = 0.6 * std::sin(t[i] + 1.0);
    }
    std::vector<double> cells(4 * 10);
    for (int i = 0; i < 40; ++i) cells[i] = std::sin(0.4 * i);
    std::vector<double> tx(N), ty(N);
    for (int i = 0; i < N; ++i) {
        tx[i] = 5.0 + 10.0 * i / (N - 1);
        ty[i] = std::cos(tx[i]);
    }

    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Shared axes"});
    fig->suptitle("sharex down the left column, sharey across the top row");

    auto s1 = fig->add_subplot(3, 2, 1);
    s1->line(t, a, {.name = "sin t"}).legend()
        .set_title("x shared (col 1), y shared (row 1)").set_ytitle("a");
    auto s3 = fig->add_subplot(3, 2, 3);
    s3->line(t, b).set_ytitle("b");
    auto s5 = fig->add_subplot(3, 2, 5);
    s5->scatter_z(t, c, b, {.colorbar = true, .name = "b"}).set_xtitle("t").set_ytitle("c");
    s3->sharex(*s1);
    s5->sharex(*s1);

    auto s2 = fig->add_subplot(3, 2, 2);
    s2->line(t, d).set_title("y shared with the left cell");
    s2->sharey(*s1);

    auto s4 = fig->add_subplot(3, 2, 4);
    s4->imshow(cells, 4, 10).set_title("heatmap: its x group stays linear");
    auto s6 = fig->add_subplot(3, 2, 6);
    s6->line(tx, ty).set_title("shares x with the heatmap").set_xtitle("column");
    s6->sharex(*s4);

    savefig_and_check_memory(fig, "test_shared_axes");
    fig->show(true);
}

// v1.2 step 36b-2: twinx()/twiny(). Check: each twin's axis on its own edge
// (cell 1's host asks for its y axis on the right and is ignored); the
// legend lists both layers in call order; drag, wheel and double-click move
// every layer; the menu bar's Host / Twin selector beside the subplot one; cell 3's
// twin labels hide toward cell 4's twin, which shares its (log) y.
static void test_twins() {
    constexpr int N = 12, M = 200;
    std::vector<double> month(N), rain(N), temp(N);
    for (int i = 0; i < N; ++i) {
        month[i] = i + 1;
        rain[i] = 40.0 + 35.0 * std::sin(0.5 * i) + 10.0 * (i % 3);
        temp[i] = 12.0 - 10.0 * std::cos(2.0 * 3.14159265 * i / N);
    }
    std::vector<double> km(M), h(M), miles(M), t(M), g(M), z(M), e(M);
    for (int i = 0; i < M; ++i) {
        km[i] = 50.0 * i / (M - 1);
        h[i] = 200.0 + 150.0 * std::sin(km[i] / 6.0) + 2.0 * km[i];
        miles[i] = km[i] / 1.609;
        t[i] = 10.0 * i / (M - 1);
        g[i] = std::sin(t[i]);
        z[i] = std::cos(0.7 * t[i]);
        e[i] = std::exp(0.5 * t[i]);
    }

    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Twins"});
    fig->suptitle("twinx / twiny: one frame, two scales, call order across both");

    // Bars on the host, the twin's line over them, then host markers over that.
    auto s1 = fig->add_subplot(2, 2, 1);
    sextant::AxesStyle right;
    right.yaxis_x = sextant::AxisPosition::High;   // ignored while twinned
    s1->set_axes_style(right);
    s1->bar(month, rain, {.name = "rain"}).set_ytitle("rain (mm)").set_xtitle("month");
    auto t1 = s1->twinx();
    t1->line(month, temp, {.linewidth = 3.0f, .name = "temperature"}).set_ytitle("temp (C)");
    s1->scatter(month, rain, {.name = "rain (points)"});
    t1->legend({.anchor = sextant::LegendAnchor::InsideTL});
    t1->set_title("twinx: legend set on the twin, both layers listed");

    // twiny: the same profile in km (bottom) and miles (top).
    auto s2 = fig->add_subplot(2, 2, 2);
    s2->line(km, h).set_xtitle("distance (km)").set_ytitle("height (m)");
    auto t2 = s2->twiny();
    t2->line(miles, h, {.linestyle = sextant::LineStyle::None}).set_xtitle("distance (miles)");
    s2->set_title("twiny: its x axis on top");
    s2->grid(true);

    // Twins sharing y across cells: cell 3's twin labels hide toward cell 4.
    auto s3 = fig->add_subplot(2, 2, 3);
    s3->line(t, g, {.name = "g"}).set_ytitle("g").set_xtitle("t");
    auto t3 = s3->twinx();
    t3->scatter_z(t, e, z, {.colorbar = true, .name = "z"});
    t3->set_yscale(sextant::Scale::Log);
    t3->set_ytitle("e (log)");
    s3->set_title("twin with a colorbar, log y shared with cell 4's twin");

    auto s4 = fig->add_subplot(2, 2, 4);
    s4->line(t, z).set_ytitle("z").set_xtitle("t");
    auto t4 = s4->twinx();
    t4->line(t, e, {.linestyle = sextant::LineStyle::Dashed}).set_ytitle("e (log)");
    t4->sharey(*t3);
    t4->grid(true);
    s4->set_title("its twin's grid draws y lines only");

    savefig_and_check_memory(fig, "test_twins");
    fig->show(true);
}

// v1.2 step 36b-3: set_aspect(). Check: each circle stays round while the
// window resizes, pans and zooms; cell 1's frame shrinks and re-centres with
// its title, ticks and legend (leaving its column's alignment); cell 2 keeps
// its frame and widens its limits; cell 3 is log-log in decades; cell 4 is a
// Datalim axes sharing x with cell 1, so its y adjusts and its x pans with
// cell 1's. The Ticks section's Aspect / Ratio / Adjust rows (Datalim greyed
// when both axes are shared).
static void test_aspect() {
    constexpr int N = 181;
    std::vector<double> cx(N), cy(N), lx(N), ly(N);
    for (int i = 0; i < N; ++i) {
        const double a = 2.0 * 3.14159265 * i / (N - 1);
        cx[i] = 3.0 + 2.0 * std::cos(a);
        cy[i] = 1.0 + 2.0 * std::sin(a);
        // A circle of one decade's radius around (100, 100) in log space.
        lx[i] = std::pow(10.0, 2.0 + std::cos(a));
        ly[i] = std::pow(10.0, 2.0 + std::sin(a));
    }
    auto fig = sextant::Figure::create({.width = 1200, .height = 850, .title = "Aspect"});
    fig->suptitle("set_aspect(1): circles stay round");

    auto s1 = fig->add_subplot(2, 2, 1);
    s1->line(cx, cy, {.name = "circle"}).set_xtitle("x").set_ytitle("y");
    s1->set_xlim(-2.0, 8.0).set_ylim(-1.0, 3.0);
    s1->set_aspect(1.0);
    s1->legend({.anchor = sextant::LegendAnchor::OutsideRT});
    s1->set_title("Box: the frame shrinks, centred");

    auto s2 = fig->add_subplot(2, 2, 2);
    s2->line(cx, cy).set_xtitle("x").set_ytitle("y");
    s2->set_aspect(1.0, sextant::Adjustable::Datalim);
    s2->set_title("Datalim: the limits widen");

    auto s3 = fig->add_subplot(2, 2, 3);
    s3->line(lx, ly).set_xscale(sextant::Scale::Log).set_yscale(sextant::Scale::Log);
    s3->set_aspect(1.0);
    s3->set_title("log-log, Box: a decade is as long on both");

    auto s4 = fig->add_subplot(2, 2, 4);
    s4->scatter(cx, cy);
    s4->sharex(*s1);
    s4->set_aspect(1.0, sextant::Adjustable::Datalim);
    s4->set_title("Datalim, x shared with cell 1: y adjusts");

    savefig_and_check_memory(fig, "test_aspect");
    fig->show(true);
}

// The 2D layout retrofit, part 1: no shared axis anywhere, so every frame is
// its own fit (cell = title over legend/colorbars over axis titles over the
// frame united with its axes' reach). Check: deleting a title, x title or y
// title in the Cosmetic panel gives its room back in that cell alone; no
// frame follows a neighbour's; cell 5's labels, poking out of the narrow
// frame past the origin, stay inside its cell.
static void test_layout_independent() {
    constexpr int N = 200;
    std::vector<double> x(N), y(N), big(N), z(N);
    for (int i = 0; i < N; ++i) {
        x[i] = 10.0 * i / (N - 1);
        y[i] = std::sin(x[i]);
        big[i] = 4.0e5 * (1.0 + std::sin(0.7 * x[i]));
        z[i] = std::cos(0.5 * x[i]);
    }

    auto fig = sextant::Figure::create({.width = 1400, .height = 950, .title = "Layout: independent"});
    fig->suptitle("no shared axes: every frame is its own fit");

    fig->add_subplot(3, 3, 1)->line(x, y).set_title("reference: title, x and y titles")
            .set_xtitle("x").set_ytitle("y");
    fig->add_subplot(2)->line(x, y).set_ytitle("y only: no title, no x title");
    fig->add_subplot(3)->line(x, big).set_title("wide y labels")
            .set_ytitle("a long y title").set_xtitle("x");

    auto s4 = fig->add_subplot(4);
    s4->line(x, y, {.name = "sin"}).line(x, z, {.name = "cos"})
            .scatter_z(x, z, y, {.colorbar = true, .name = "z"})
            .legend({.anchor = sextant::LegendAnchor::OutsideRT})
            .set_colorbar_style({.anchor = sextant::ColorbarAnchor::Bottom})
            .set_title("legend right, colorbar below").set_xtitle("x");

    {
        sextant::AxesStyle st;
        st.origin_x = 0.0;
        st.origin_y = 0.0;
        fig->add_subplot(5)->line(x, y).set_axes_style(st).set_xlim(-0.2, 10.0).set_ylim(-0.04, 1.2)
                .set_title("origin: labels poke out").set_xtitle("x").set_ytitle("y");
    }
    {
        sextant::AxesStyle st;
        st.xaxis_y = sextant::AxisPosition::High;
        st.yaxis_x = sextant::AxisPosition::High;
        fig->add_subplot(6)->line(x, y).set_axes_style(st)
                .set_title("High / High").set_xtitle("x").set_ytitle("y");
    }
    {
        sextant::AxesStyle st;
        st.show_xticks = false;
        st.show_ytick_labels = false;
        fig->add_subplot(7)->line(x, y).set_axes_style(st)
                .set_title("x hidden, y labels hidden").set_xtitle("x").set_ytitle("y");
    }

    auto s8 = fig->add_subplot(8);
    s8->line(x, y).set_xtitle("x (host)").set_ytitle("y (host)");
    s8->twinx()->line(x, big).set_ytitle("big (twinx)");
    s8->twiny()->line(big, y).set_xtitle("big (twiny)");
    s8->set_title("twinx + twiny: four axis titles");

    auto s9 = fig->add_subplot(9);
    s9->line(x, y).set_aspect(4.0).set_title("set_aspect(4), Box").set_xtitle("x").set_ytitle("y");

    savefig_and_check_memory(fig, "test_layout_independent");
    fig->show(true);
}

// The 2D layout retrofit, part 2: alignment, only where an axis is shared
// (twins included), on the same grid line, with no set_aspect(). Check:
// column 1 (sharex) keeps one x span, set by cell 1's wide labels and cell
// 4's left legend; row 1 (sharey) keeps one y span, set by cell 3's top
// colorbar, while each title stays on its own frame; cells 5 and 6 align
// through their twins' shared y; cell 9 shares y with cell 8 but its aspect
// takes both out of alignment. Deleting cell 3's colorbar (or cell 1's
// title) moves its whole row back.
static void test_layout_aligned() {
    constexpr int N = 200;
    std::vector<double> x(N), y(N), big(N), z(N), e(N);
    for (int i = 0; i < N; ++i) {
        x[i] = 10.0 * i / (N - 1);
        y[i] = std::sin(x[i]);
        big[i] = 4.0e5 * (1.0 + std::sin(0.7 * x[i]));
        z[i] = std::cos(0.5 * x[i]);
        e[i] = std::exp(0.5 * x[i]);
    }

    auto fig = sextant::Figure::create({.width = 1400, .height = 950, .title = "Layout: aligned"});
    fig->suptitle("aligned only along a shared axis");

    auto s1 = fig->add_subplot(3, 3, 1);
    s1->line(x, big).set_title("sharex col 1, sharey row 1").set_ytitle("big");
    auto s4 = fig->add_subplot(4);
    s4->line(x, y, {.name = "sin"}).legend({.anchor = sextant::LegendAnchor::OutsideLT});
    s4->sharex(*s1);
    auto s7 = fig->add_subplot(7);
    s7->line(x, z).set_xtitle("x, shared down the column");
    s7->sharex(*s1);

    auto s2 = fig->add_subplot(2);
    s2->line(x, big).set_title("no title above, aligned anyway");
    s2->sharey(*s1);
    auto s3 = fig->add_subplot(3);
    s3->scatter_z(x, big, z, {.colorbar = true, .name = "z"})
            .set_colorbar_style({.anchor = sextant::ColorbarAnchor::Top});
    s3->sharey(*s1);

    auto s5 = fig->add_subplot(5);
    s5->line(x, y).set_ytitle("y");
    auto t5 = s5->twinx();
    t5->line(x, e).set_ytitle("e");
    s5->set_title("twins share y with cell 6's");
    auto s6 = fig->add_subplot(6);
    s6->line(x, z);
    auto t6 = s6->twinx();
    t6->line(x, e, {.linestyle = sextant::LineStyle::Dashed}).set_ytitle("e, with a longer twin title");
    t6->sharey(*t5);
    s6->set_xtitle("x");

    auto s8 = fig->add_subplot(8);
    s8->line(x, y).set_title("shares y with cell 9").set_xtitle("x");
    auto s9 = fig->add_subplot(9);
    s9->line(x, y).set_aspect(3.0).set_title("set_aspect(3): not aligned");
    s9->sharey(*s8);

    savefig_and_check_memory(fig, "test_layout_aligned");
    fig->show(true);
}

// v1.2 steps 36h/36i: per-axis tick and label looks, twinned axes in their
// series' colors, and a twin's setters reaching its host. Check: cell 1's
// left axis (marks, labels, title, edge) all blue and the right all orange,
// one x title ("month", set on the twin), a pale yellow plot area (set on the
// twin, the cell's); cell 2's bottom edge green, top purple; cell 3's x marks
// long red over large labels, the y axis small with short minor marks; cell
// 4's z marks and labels red. The Cosmetic panel's Ticks & labels table
// (Both | X | Y [| Z]) edits these, an unset axis dimmed; on a twin layer the
// shared axis' column is greyed and its title row gone.
static void test_tick_style() {
    constexpr int N = 12, M = 120;
    std::vector<double> month(N), rain(N), temp(N), x(M), y(M), d(M);
    for (int i = 0; i < N; ++i) {
        month[i] = i + 1;
        rain[i] = 40.0 + 35.0 * std::sin(0.5 * i) + 10.0 * (i % 3);
        temp[i] = 12.0 - 10.0 * std::cos(2.0 * 3.14159265 * i / N);
    }
    for (int i = 0; i < M; ++i) {
        x[i] = 0.1 + 9.9 * i / (M - 1);
        y[i] = std::exp(0.6 * x[i]);
        d[i] = x[i] * 1.609;
    }
    const sextant::Color blue = sextant::Color::Blue, orange = sextant::Color::Orange;

    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Tick style"});
    fig->suptitle("Per-axis ticks and labels, twinned axes in their series' colors");

    // matplotlib's twinx look: each y axis in its series' color.
    auto s1 = fig->add_subplot(2, 2, 1);
    s1->line(month, rain, {.color = blue, .linewidth = 2.5f}).set_ytitle("rain (mm)");
    sextant::AxesStyle hs;
    hs.ytick_color = hs.ylabel_color = hs.yaxis_color = blue;
    hs.ytitle_color = blue;
    s1->set_axes_style(hs);
    s1->set_ytitle("rain (mm)");
    auto t1 = s1->twinx();
    t1->line(month, temp, {.color = orange, .linewidth = 2.5f});
    sextant::AxesStyle ts;
    ts.ytick_color = ts.ylabel_color = ts.yaxis_color = orange;
    ts.ytitle_color = orange;
    ts.background = {1.0f, 0.98f, 0.88f, 1.0f};   // the cell's: reaches the host
    ts.spine_linewidth = 2.0f;                      // likewise
    t1->set_axes_style(ts);
    t1->set_ytitle("temp (C)");
    t1->set_xtitle("month");                       // the host's x axis
    t1->set_title("twinx: y axes in their series' colors");

    // twiny: bottom and top edges in their own colors.
    auto s2 = fig->add_subplot(2, 2, 2);
    s2->line(x, y).set_xtitle("distance (km)").set_ytitle("height");
    auto t2 = s2->twiny();
    t2->line(d, y, {.linestyle = sextant::LineStyle::None}).set_xtitle("distance (miles)");
    sextant::AxesStyle h2;
    h2.xaxis_color = h2.xtick_color = h2.xlabel_color = sextant::Color::Green;
    h2.spine_linewidth = 2.0f;
    s2->set_axes_style(h2);
    s2->set_xtitle("distance (km)");
    sextant::AxesStyle tw2;
    tw2.xaxis_color = tw2.xtick_color = tw2.xlabel_color = sextant::Color::Purple;
    tw2.spine_linewidth = 2.0f;
    t2->set_axes_style(tw2);
    t2->set_xtitle("distance (miles)");
    s2->set_title("twiny: bottom green, top purple");

    // One axes, each axis its own look.
    auto s3 = fig->add_subplot(2, 2, 3);
    s3->line(x, y).set_yscale(sextant::Scale::Log);
    sextant::AxesStyle p;
    p.xtick_color = sextant::Color::Red;
    p.xtick_length = 10.0f;
    p.xtick_linewidth = 2.5f;
    p.xlabel_fontsize = 16.0f;
    p.ytick_length = 3.0f;
    p.minor_ytick_length = 1.5f;
    p.ylabel_fontsize = 9.0f;
    p.ylabel_color = sextant::Color::Gray;
    s3->set_axes_style(p);
    s3->set_title("x: long red marks, big labels; y: small").set_xtitle("x").set_ytitle("exp(0.6 x)");

    // 3D: the z axis alone in red.
    auto s4 = fig->add_subplot3d(2, 2, 4);
    std::vector<double> u(20), v(20), hgt(400);
    for (int i = 0; i < 20; ++i) u[i] = v[i] = -2.0 + 4.0 * i / 19.0;
    for (int j = 0; j < 20; ++j)
        for (int i = 0; i < 20; ++i) hgt[j * 20 + i] = std::exp(-(u[i] * u[i] + v[j] * v[j]));
    s4->surface(sextant::PlaneOrientation::XY, u, v, hgt, {.colormap = true});
    sextant::AxesStyle z;
    z.ztick_color = z.zlabel_color = sextant::Color::Red;
    z.ztick_length = 9.0f;
    z.ztick_linewidth = 2.0f;
    z.zlabel_fontsize = 14.0f;
    s4->set_axes_style(z);
    s4->set_title("3D: z marks and labels red");

    savefig_and_check_memory(fig, "test_tick_style");
    fig->show(true);
}

// Step 36c-1: every marker shape, filled / outlined / hollow; the line markers
// at their default and wider strokes; legend keys; scatter_z, 3D and a plane.
static void test_marker_shapes() {
    using sextant::MarkerStyle;
    const MarkerStyle all[] = {
        MarkerStyle::Circle, MarkerStyle::Square, MarkerStyle::Triangle, MarkerStyle::TriangleDown,
        MarkerStyle::TriangleLeft, MarkerStyle::TriangleRight, MarkerStyle::Diamond, MarkerStyle::Star,
        MarkerStyle::Pentagon, MarkerStyle::Hexagon, MarkerStyle::Cross, MarkerStyle::Plus,
        MarkerStyle::ThinCross, MarkerStyle::ThinPlus, MarkerStyle::VLine, MarkerStyle::HLine
    };
    const char* names[] = {"o", "s", "^", "v", "<", ">", "D", "*", "p", "h", "X", "P", "x", "+", "|", "_"};
    constexpr int K = 16;

    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Marker shapes"});
    fig->suptitle("Marker shapes: filled, outlined, hollow; line markers stroked");

    // Rows: filled, orange outline 3 px, hollow (line markers: 3 px stroke).
    auto a1 = fig->add_subplot(2, 2, 1);
    for (int i = 0; i < K; ++i) {
        const bool line = i >= 12;
        const std::vector<double> x{i + 1.0}, y0{3.0}, y1{2.0}, y2{1.0};
        a1->scatter(x, y0, {.color = sextant::Color::Blue, .size = 28.0f, .marker = all[i], .alpha = 1.0f});
        a1->scatter(x, y1, {.color = sextant::Color::Blue, .size = 28.0f, .marker = all[i], .alpha = 1.0f,
                            .edgecolor = sextant::Color::Orange, .edge_linewidth = 3.0f});
        a1->scatter(x, y2, {.color = sextant::Color::Blue, .size = 28.0f, .marker = all[i],
                            .alpha = line ? 1.0f : 0.0f, .edge_linewidth = 3.0f});
        a1->text(names[i], sextant::Pos(i + 1.0), sextant::Pos(3.7), {.ha = sextant::HAlign::Center});
    }
    a1->set_xlim(0, K + 1).set_ylim(0.3, 4.2);
    a1->set_title("filled / orange outline (line markers ignore it) / hollow, 3 px");

    // One series per shape, keyed: the legend keys match the picture.
    auto a2 = fig->add_subplot(2, 2, 2);
    for (int i = 0; i < K; ++i) {
        std::vector<double> x(8), y(8);
        for (int j = 0; j < 8; ++j) {
            x[j] = j;
            y[j] = i + 0.3 * std::sin(0.9 * j + i);
        }
        a2->scatter(x, y, {.size = 14.0f, .marker = all[i], .name = names[i], .alpha = 1.0f});
    }
    a2->legend({.anchor = sextant::LegendAnchor::OutsideRT}).set_title("legend keys (color cycle)");

    // scatter_z: per-point colors; line markers take each point's color too.
    auto a3 = fig->add_subplot(2, 2, 3);
    constexpr int N = 60;
    std::vector<double> x(N), y(N), z(N);
    for (int i = 0; i < N; ++i) {
        x[i] = std::cos(0.21 * i) * (1.0 + 0.02 * i);
        y[i] = std::sin(0.21 * i) * (1.0 + 0.02 * i);
        z[i] = i / (N - 1.0);
    }
    a3->scatter_z(x, y, z, {.size = 22.0f, .marker = MarkerStyle::Star, .alpha = 1.0f,
                            .edgecolor = sextant::Color::Black, .edge_linewidth = 1.0f,
                            .colorbar = true, .name = "star"});
    std::vector<double> x2(N), y2(N);
    for (int i = 0; i < N; ++i) {
        x2[i] = 0.5 * x[i];
        y2[i] = 0.5 * y[i];
    }
    a3->scatter_z(x2, y2, z, {.cmap = sextant::Colormap::Plasma, .size = 16.0f,
                              .marker = MarkerStyle::ThinCross, .alpha = 1.0f, .edge_linewidth = 2.0f,
                              .name = "x, 2 px"});
    a3->set_aspect(1.0).legend().set_title("scatter_z: Star outlined, ThinCross 2 px");

    // 3D cloud and a plane: the shapes on billboards and drawn into a plane.
    auto a4 = fig->add_subplot3d(2, 2, 4);
    std::vector<double> cx, cy, cz;
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) {
            cx.push_back(i);
            cy.push_back(j);
            cz.push_back(1.0 + 0.3 * std::sin(i + j));
        }
    a4->scatter3d(cx, cy, cz, {.color = sextant::Color::Green, .size = 18.0f, .marker = MarkerStyle::Hexagon,
                               .edgecolor = sextant::Color::Black, .edge_linewidth = 1.5f, .name = "hexagon"});
    auto pl = a4->plane(sextant::PlaneOrientation::XY, 0.0);
    for (int i = 0; i < 6; ++i) {
        const std::vector<double> px{0.5 + i}, py{2.5};
        pl->scatter(px, py, {.color = sextant::Color::Red, .size = 26.0f, .marker = all[3 + i * 2],
                            .alpha = 1.0f, .edge_linewidth = i % 2 ? 0.0f : 3.0f});
    }
    a4->legend().set_title("3D: hexagons; plane at z=0 (v < D p X x)");

    savefig_and_check_memory(fig, "test_marker_shapes");
    fig->show(true);
}

// Step 36c-2: per-point marker sizes -- a bubble chart, hidden points, a
// plane and a 3D cloud; the Data panel's size column.
static void test_marker_sizes() {
    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Marker sizes"});
    fig->suptitle("Per-point marker sizes (ScatterOptions::sizes)");

    // A bubble chart: size and color both carry a value.
    auto a1 = fig->add_subplot(2, 2, 1);
    {
        constexpr int N = 40;
        std::vector<double> x(N), y(N), z(N);
        sextant::ScatterZOptions o;
        for (int i = 0; i < N; ++i) {
            x[i] = 1.0 + 8.0 * std::fmod(0.618034 * i, 1.0);
            y[i] = 1.0 + 8.0 * std::fmod(0.414214 * i * i, 1.0);
            z[i] = std::sin(0.3 * i) * 0.5 + 0.5;
            o.sizes.push_back(8.0 + 50.0 * z[i]);
        }
        o.alpha = 0.6f;
        o.edgecolor = sextant::Color::Black;
        o.edge_linewidth = 1.0f;
        o.colorbar = true;
        o.name = "value";
        a1->scatter_z(x, y, z, o).set_xlim(0, 10).set_ylim(0, 10);
        a1->set_title("bubble chart: size and color from one value");
    }

    // Growing sizes along a line, with NaN / 0 / negative points hidden (gaps).
    auto a2 = fig->add_subplot(2, 2, 2);
    {
        std::vector<double> x, y;
        sextant::ScatterOptions o;
        o.marker = sextant::MarkerStyle::Square;
        o.name = "squares";
        for (int i = 0; i < 12; ++i) {
            x.push_back(i);
            y.push_back(0.5 * i);
            o.sizes.push_back(i == 3 ? std::nan("") : i == 6 ? 0.0 : i == 9 ? -5.0 : 4.0 + 3.0 * i);
        }
        a2->scatter(x, y, o);
        sextant::ScatterOptions s;
        s.marker = sextant::MarkerStyle::Star;
        s.sizes = {10, 20, 30, 40, 30, 20, 10};
        s.name = "stars";
        a2->scatter(std::vector<double>{1, 2, 3, 4, 5, 6, 7}, std::vector<double>{5, 5.5, 6, 6.5, 6, 5.5, 5}, s);
        a2->legend().set_title("sizes per point; rows 3, 6, 9 hidden (NaN, 0, -5)");
    }

    // A plane: sizes drawn into the plane (they foreshorten with it).
    auto a3 = fig->add_subplot3d(2, 2, 3);
    {
        auto pl = a3->plane(sextant::PlaneOrientation::XY, 0.0);
        std::vector<double> x, y;
        sextant::ScatterOptions o;
        o.color = sextant::Color::Red;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j) {
                x.push_back(i * 2.0 + 1.0);
                y.push_back(j * 2.0 + 1.0);
                o.sizes.push_back(6.0 + 3.0 * (i + j));
            }
        pl->scatter(x, y, o);
        a3->set_xlim(0, 10).set_ylim(0, 10).set_zlim(0, 1);
        a3->set_title("plane: sizes 6..30 across the grid");
    }

    // A 3D cloud: billboards at their own sizes.
    auto a4 = fig->add_subplot3d(2, 2, 4);
    {
        std::vector<double> x, y, z;
        sextant::Scatter3DOptions o;
        o.edgecolor = sextant::Color::Black;
        o.edge_linewidth = 1.0f;
        o.name = "cloud";
        for (int i = 0; i < 60; ++i) {
            const double t = 0.35 * i;
            x.push_back(std::cos(t) * (1.0 + 0.05 * i));
            y.push_back(std::sin(t) * (1.0 + 0.05 * i));
            z.push_back(0.1 * i);
            o.sizes.push_back(5.0 + 0.6 * i);
        }
        a4->scatter3d(x, y, z, o).legend().set_title("3D: sizes 5..40 up the helix");
    }

    savefig_and_check_memory(fig, "test_marker_sizes");
    fig->show(true);
}

// Step drawstyles (LineOptions::drawstyle): the four modes over the same points,
// a log axis with NaN gaps and error bars, a looped staircase, and a plane.
static void test_line_steps() {
    using sextant::DrawStyle;
    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Line steps"});
    fig->suptitle("Step drawstyle (LineOptions::drawstyle)");

    const std::vector<double> x{0, 1, 2, 3, 4, 5, 6, 7};
    const std::vector<double> y{1, 3, 2, 4, 3.5, 1.5, 2.5, 2};

    // The four modes, offset; markers on the points show where each one rises.
    auto a1 = fig->add_subplot(2, 2, 1);
    {
        const struct {
            DrawStyle ds;
            const char* name;
        } modes[] = {{DrawStyle::Default, "default"}, {DrawStyle::StepsPre, "steps_pre"},
                     {DrawStyle::StepsMid, "steps_mid"}, {DrawStyle::StepsPost, "steps_post"}};
        double off = 0.0;
        for (const auto& m: modes) {
            std::vector<double> yy(y);
            for (double& v: yy) v += off;
            sextant::LineOptions o;
            o.linewidth = 2.0f;
            o.drawstyle = m.ds;
            o.name = m.name;
            a1->line(x, yy, o);
            a1->scatter(x, yy, {.color = sextant::Color::Black, .size = 6.0f, .show_legend = false});
            off += 4.5;
        }
        a1->set_title("the four modes over the same points (black dots)");
    }

    // A log x axis: mid rises at the data-space mean (right of the visual
    // middle). A NaN breaks the run; error bars stay on the points; dashed.
    auto a2 = fig->add_subplot(2, 2, 2);
    {
        const std::vector<double> lx{1, 3, 10, 30, 100, 300, 1000};
        const std::vector<double> ly{2, 4, 3, std::nan(""), 5, 3.5, 4.5};
        const std::vector<double> e{0.3, 0.4, 0.3, 0.0, 0.5, 0.3, 0.4};
        a2->line(lx, ly, {.y_cap_lo = e, .y_cap_hi = e},
                 {.linewidth = 2.0f, .name = "steps_mid, log x", .drawstyle = DrawStyle::StepsMid});
        std::vector<double> ly2(ly);
        for (double& v: ly2) v -= 1.5;
        a2->line(lx, ly2, {.linewidth = 2.0f, .linestyle = sextant::LineStyle::Dashed,
                           .name = "steps_post, dashed", .drawstyle = DrawStyle::StepsPost});
        a2->set_xscale(sextant::Scale::Log);
        a2->set_title("log x: NaN at 30 breaks both; error bars on the points");
    }

    // A looped staircase: the closing pair steps too.
    auto a3 = fig->add_subplot(2, 2, 3);
    {
        const std::vector<double> sx{0, 2, 4, 6, 4, 2};
        const std::vector<double> sy{0, 2, 4, 2, 6, 4};
        a3->line(sx, sy, {.linewidth = 3.0f, .name = "steps_pre, loop", .loop = true,
                          .drawstyle = DrawStyle::StepsPre});
        a3->line(sx, sy, {.linewidth = 1.0f, .linestyle = sextant::LineStyle::Dotted,
                          .name = "the points (straight, loop)", .loop = true});
        a3->set_title("loop: the closing segment (2,4) -> (0,0) is stepped");
    }

    // A plane: the same expansion, drawn into the plane.
    auto a4 = fig->add_subplot3d(2, 2, 4);
    {
        auto pl = a4->plane(sextant::PlaneOrientation::XY, 0.0);
        pl->line(x, y, {.color = sextant::Color::Red, .linewidth = 2.5f, .name = "plane steps_mid",
                        .drawstyle = DrawStyle::StepsMid});
        a4->set_xlim(0, 7).set_ylim(0, 5).set_zlim(0, 1);
        a4->set_title("plane: steps_mid");
    }

    savefig_and_check_memory(fig, "test_line_steps");
    fig->show(true);
}

// Horizontal bars and bar bases (BarOptions::orientation, BarOptions::bottom):
// stacked, a waterfall, lying bars with error bars, a horizontal hist, log x,
// and a plane. The Data panel's Orientation combo flips any of them.
static void test_bar_orientation() {
    using sextant::Orientation;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Bar orientation"});
    fig->suptitle("Horizontal bars and bottom (BarOptions::orientation, BarOptions::bottom)");

    const std::vector<double> cat{1, 2, 3, 4, 5};

    // Three series stacked: each one's bottom is the sum below it.
    auto a1 = fig->add_subplot(2, 3, 1);
    {
        const std::vector<double> s1{3, 4, 2, 5, 3}, s2{2, 1, 3, 2, 2}, s3{1, 2, 1, 1, 3};
        std::vector<double> b2(s1), b3(s1);
        for (std::size_t i = 0; i < b3.size(); ++i) b3[i] += s2[i];
        a1->bar(cat, s1, {.name = "first"});
        a1->bar(cat, s2, {.bottom = b2, .name = "second"});
        a1->bar(cat, s3, {.bottom = b3, .name = "third"});
        a1->legend();
        a1->set_title("stacked: bottom = the series below");
    }

    // A waterfall: each step starts where the last ended; negative steps hang down.
    auto a2 = fig->add_subplot(2, 3, 2);
    {
        const std::vector<double> step{5, 3, -2, 4, -3, -1};
        std::vector<double> x(step.size()), base(step.size());
        double run = 0.0;
        for (std::size_t i = 0; i < step.size(); ++i) {
            x[i] = static_cast<double>(i);
            base[i] = run;
            run += step[i];
        }
        a2->bar(x, step, {.bottom = base, .width = 0.6f, .name = "steps", .linewidth = 1.0f});
        a2->set_title("waterfall: negative heights run down from their base");
    }

    // Lying bars from a common left edge, with x errors at the tip and labels.
    auto a3 = fig->add_subplot(2, 3, 3);
    {
        const std::vector<double> w{4.0, 6.5, 3.0, 7.5, 5.0};
        const std::vector<double> e{0.5, 0.8, 0.4, 1.0, 0.6};
        a3->bar(cat, w, {.x_cap_lo = e, .x_cap_hi = e},
                {.color = sextant::Color{0.2f, 0.6f, 0.4f, 1.0f}, .orientation = Orientation::Horizontal,
                 .bottom = {1.0}, .width = 0.6f, .name = "left = 1",
                 .hint_labels = {"alpha", "beta", "gamma", "delta", "epsilon"}});
        a3->set_title("horizontal, left = 1, x errors at the tip (hover: y, width, left)");
    }

    // A horizontal histogram through hist()'s bar_opts.
    auto a4 = fig->add_subplot(2, 3, 4);
    {
        std::vector<double> data;
        for (int i = 0; i < 2000; ++i) {
            double s = 0.0;
            for (int k = 0; k < 6; ++k) s += static_cast<double>((i * 7919 + k * 104729) % 1000) / 1000.0;
            data.push_back(s - 3.0);
        }
        a4->hist(data, 24, {.orientation = Orientation::Horizontal, .width = 0.9f, .name = "counts"});
        a4->set_title("hist(orientation = Horizontal)");
    }

    // Log x: a base of 0 has no place, a set base does.
    auto a5 = fig->add_subplot(2, 3, 5);
    {
        a5->bar(cat, std::vector<double>{9, 90, 900, 40, 4},
                {.orientation = Orientation::Horizontal, .bottom = {1.0}, .name = "left = 1"});
        a5->set_xscale(sextant::Scale::Log);
        a5->set_title("log x: bars from 1 to 10, 100, 1000 ...");
    }

    // A plane: lying, stacked bars drawn into it.
    auto a6 = fig->add_subplot3d(2, 3, 6);
    {
        auto pl = a6->plane(sextant::PlaneOrientation::XY, 0.0);
        const std::vector<double> u{2, 3, 1, 4, 2}, v{1, 1, 2, 1, 2};
        pl->bar(cat, u, {.color = sextant::Color::Red, .orientation = Orientation::Horizontal, .name = "u"});
        pl->bar(cat, v, {.color = sextant::Color::Blue, .orientation = Orientation::Horizontal, .bottom = u,
                         .name = "v on u"});
        a6->set_xlim(0, 7).set_ylim(0, 6).set_zlim(0, 1);
        a6->set_title("plane: stacked, horizontal");
    }

    savefig_and_check_memory(fig, "test_bar_orientation");
    fig->show(true);
}

// Step 36c-5: Axes::axline()/axspan(), both orientations, frame fractions,
// dashes, outlines, legend keys, call order, twins, log and live movement.
static void test_reference_lines() {
    using sextant::Orientation;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Reference lines and spans"});
    fig->suptitle("Reference lines and spans (Axes::axline, Axes::axspan)");

    std::vector<double> x(200), y(200);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<double>(i) * 0.05;
        y[i] = std::sin(x[i]) * 2.0 + 0.3 * std::sin(7.0 * x[i]);
    }

    // The basics: a threshold, a mean, a band of tolerance, a shaded interval.
    auto a1 = fig->add_subplot(2, 3, 1);
    {
        a1->axspan(-1.0, 1.0, {.name = "tolerance"});
        a1->line(x, y, {.name = "signal"});
        a1->axline(1.5, {.color = sextant::Color::Red, .linestyle = sextant::LineStyle::Dashed, .name = "threshold"});
        a1->axline(0.0, {.color = sextant::Color::Black, .linewidth = 1.0f});
        a1->axspan(3.0, 5.0, {.orientation = Orientation::Vertical, .color = sextant::Color::Gray, .alpha = 0.3f,
                              .name = "window"});
        a1->axline(7.5, {.orientation = Orientation::Vertical, .linestyle = sextant::LineStyle::DashDot,
                         .name = "event"});
        a1->legend();
        a1->set_title("h/v lines and spans; limits take the values (pan: they stay across)");
    }

    // Frame fractions: lines and spans covering part of the frame.
    auto a2 = fig->add_subplot(2, 3, 2);
    {
        a2->scatter(x, y, {.size = 6.0f});
        a2->axline(1.0, {.color = sextant::Color::Red, .linewidth = 3.0f, .from = 0.0, .to = 0.5, .name = "left half"});
        a2->axline(-1.0, {.color = sextant::Color::Blue, .linewidth = 3.0f, .from = 0.5, .to = 1.0,
                          .name = "right half"});
        a2->axspan(4.0, 6.0, {.orientation = Orientation::Vertical, .color = sextant::Color{0.2f, 0.7f, 0.3f, 1.0f},
                              .alpha = 0.4f, .edgecolor = sextant::Color{0.1f, 0.4f, 0.1f, 1.0f}, .linewidth = 1.5f,
                              .from = 0.25, .to = 0.75, .name = "outlined, middle half"});
        a2->legend();
        a2->set_title("from/to fractions of the frame; an outlined span");
    }

    // Call order: an opaque span drawn after the line covers it, a line after it crosses it.
    auto a3 = fig->add_subplot(2, 3, 3);
    {
        a3->line(x, y, {.linewidth = 3.0f, .name = "line first"});
        a3->axspan(2.0, 4.0, {.orientation = Orientation::Vertical, .color = sextant::Color{1.0f, 0.8f, 0.3f, 1.0f},
                              .alpha = 1.0f, .name = "opaque span second"});
        a3->axline(0.5, {.color = sextant::Color::Red, .linewidth = 2.0f, .name = "line third"});
        a3->legend();
        a3->set_title("call order: the span hides the line under it");
    }

    // A twin: each line on its own y axis; a vertical one on the shared x.
    auto a4 = fig->add_subplot(2, 3, 4);
    {
        a4->line(x, y, {.name = "left y"});
        a4->axline(1.0, {.linestyle = sextant::LineStyle::Dashed, .name = "y = 1 (left)"});
        auto tw = a4->twinx();
        std::vector<double> big(y);
        for (double& v : big) v *= 100.0;
        tw->line(x, big, {.name = "right y"});
        tw->axline(-150.0, {.linestyle = sextant::LineStyle::Dashed, .name = "y = -150 (right)"});
        tw->axline(9.0, {.orientation = Orientation::Vertical, .color = sextant::Color::Black, .name = "x = 9"});
        a4->legend();
        a4->set_title("twinx: each on its own y; x shared");
    }

    // Log y: a line at 0 is not drawn, a span from 0 reaches below the frame.
    auto a5 = fig->add_subplot(2, 3, 5);
    {
        std::vector<double> e(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) e[i] = std::exp(x[i]);
        a5->line(x, e, {.name = "exp(x)"});
        a5->axline(100.0, {.color = sextant::Color::Red, .name = "y = 100"});
        a5->axline(0.0, {.name = "y = 0 (not drawn)"});
        a5->axspan(0.0, 10.0, {.name = "0 .. 10"});
        a5->set_yscale(sextant::Scale::Log);
        a5->legend();
        a5->set_title("log y: 0 has no place; a span from 0 runs off the bottom");
    }

    // Edit in the Data panel: value/ends in the table, orientation, from/to, style.
    auto a6 = fig->add_subplot(2, 3, 6);
    {
        a6->scatter(x, y, {.size = 5.0f});
        a6->axline(0.0, {.name = "edit me (Data panel)"});
        a6->axspan(1.0, 2.0, {.name = "edit me too"});
        a6->legend();
        a6->set_title("Data panel: value, ends, orientation, from/to");
    }

    savefig_and_check_memory(fig, "test_reference_lines");
    fig->show(true);
}

// Step 36d-1: Axes::fill_between() and Plane2D::fill_between(): areas under a
// curve and bands, exclude and interpolate, steps, the Vertical orientation
// with an outline, log axes and NaN gaps, planes; the Data panel edits them.
static void test_fill_between() {
    using sextant::Orientation;
    using V = std::vector<double>;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Filled areas"});
    fig->suptitle("Filled areas (Axes::fill_between, Plane2D::fill_between)");

    V x(200), y(200), lo(200), hi(200);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<double>(i) * 0.05;
        y[i] = std::sin(x[i]) * 2.0 + 0.3 * std::sin(7.0 * x[i]);
        const double spread = 0.3 + 0.15 * x[i];
        lo[i] = y[i] - spread;
        hi[i] = y[i] + spread;
    }

    // An area under a curve, and an error band under its line.
    auto a1 = fig->add_subplot(2, 3, 1);
    {
        V base(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) base[i] = 0.5 + 0.4 * std::cos(x[i]);
        a1->fill(x, base, 0.0, {.color = sextant::Color::Gray, .alpha = 0.4f, .name = "area to 0"});
        a1->fill(x, lo, hi, {.alpha = 0.3f, .name = "band"});
        a1->line(x, y, {.name = "signal"});
        a1->legend();
        a1->set_title("area to 0; a band under its line (edit in the Data panel)");
    }

    // Exclude and interpolate: green where y1 > y2 runs on to the crossings,
    // red where y1 < y2 stops at the last kept point.
    auto a2 = fig->add_subplot(2, 3, 2);
    {
        V y1(x.size()), y2(x.size());
        std::vector<bool> below(x.size()), above(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            y1[i] = std::sin(x[i] * 1.3);
            y2[i] = 0.4 * std::cos(x[i] * 0.7);
            below[i] = !(y1[i] > y2[i]);
            above[i] = !(y1[i] < y2[i]);
        }
        // Sparse points, so the steps at the crossings show.
        V xs, s1, s2;
        std::vector<bool> ex_b, ex_a;
        for (std::size_t i = 0; i < x.size(); i += 8) {
            xs.push_back(x[i]);
            s1.push_back(y1[i]);
            s2.push_back(y2[i]);
            ex_b.push_back(below[i]);
            ex_a.push_back(above[i]);
        }
        a2->fill(xs, s1, s2, {.color = sextant::Color{0.2f, 0.7f, 0.3f, 1.0f}, .alpha = 0.6f,
                                      .exclude = ex_b, .interpolate = true, .name = "y1 > y2, interpolate"});
        a2->fill(xs, s1, s2, {.color = sextant::Color::Red, .alpha = 0.6f, .exclude = ex_a,
                                      .name = "y1 < y2, no interpolate"});
        a2->line(xs, s1, {.color = sextant::Color::Black, .linewidth = 1.0f});
        a2->line(xs, s2, {.color = sextant::Color::Black, .linewidth = 1.0f, .linestyle = sextant::LineStyle::Dashed});
        a2->legend();
        a2->set_title("exclude (matplotlib's where, inverted) and interpolate");
    }

    // Steps: the same fill pre, mid and post, each over its step line.
    auto a3 = fig->add_subplot(2, 3, 3);
    {
        const V sx{0, 1, 2, 3, 4, 5};
        const V sy{1, 3, 2, 4, 3, 5};
        const sextant::DrawStyle styles[] = {sextant::DrawStyle::StepsPre, sextant::DrawStyle::StepsMid,
                                             sextant::DrawStyle::StepsPost};
        const char* names[] = {"steps_pre", "steps_mid", "steps_post"};
        for (int k = 0; k < 3; ++k) {
            V shifted(sy);
            for (double& v : shifted) v += 6.0 * k;
            a3->fill(sx, shifted, 6.0 * k, {.alpha = 0.4f, .drawstyle = styles[k], .name = names[k]});
            a3->line(sx, shifted, {.color = sextant::Color::Black, .linewidth = 1.0f, .drawstyle = styles[k]});
            a3->scatter(sx, shifted, {.color = sextant::Color::Black, .size = 5.0f});
        }
        a3->legend();
        a3->set_title("drawstyle: pre, mid, post");
    }

    // Vertical: fill_betweenx, outlined and dashed.
    auto a4 = fig->add_subplot(2, 3, 4);
    {
        V depth(60), t1(60), t2(60);
        for (std::size_t i = 0; i < depth.size(); ++i) {
            depth[i] = static_cast<double>(i) * 0.5;
            t1[i] = 10.0 + 6.0 * std::exp(-depth[i] / 8.0);
            t2[i] = t1[i] - 2.0 - 0.5 * std::sin(depth[i]);
        }
        a4->fill(depth, t2, t1, {.orientation = Orientation::Vertical, .alpha = 0.35f,
                                         .edgecolor = sextant::Color::Black, .linewidth = 1.5f,
                                         .linestyle = sextant::LineStyle::Dashed, .name = "range at depth"});
        a4->line(t1, depth, {.name = "max"});
        a4->set_ylim(30.0, 0.0);
        a4->legend();
        a4->set_title("Vertical (fill_betweenx), dashed outline, y reversed");
    }

    // Log y: the area to 0 runs off the bottom; NaN points leave gaps.
    auto a5 = fig->add_subplot(2, 3, 5);
    {
        V e(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) e[i] = std::exp(x[i]);
        V gappy(e);
        for (std::size_t i = 60; i < 80; ++i) gappy[i] = std::nan("");
        a5->fill(x, gappy, 0.0, {.alpha = 0.4f, .name = "exp(x) to 0, NaN gap"});
        a5->line(x, e, {.name = "exp(x)"});
        a5->set_yscale(sextant::Scale::Log);
        a5->legend();
        a5->set_title("log y: y2 = 0 runs off the bottom; a NaN run is a gap");
    }

    // Planes: a band on the floor, an area on a wall.
    auto a6 = fig->add_subplot3d(2, 3, 6);
    {
        auto floor = a6->plane(sextant::PlaneOrientation::XY, 0.0);
        floor->fill(x, lo, hi, {.alpha = 0.5f, .name = "band on XY"});
        floor->line(x, y, {.name = "signal on XY"});
        auto wall = a6->plane(sextant::PlaneOrientation::ZX, 3.0);
        V zs(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) zs[i] = 1.0 + std::sin(x[i]) * 0.8;
        wall->fill(x, zs, 0.0, {.orientation = Orientation::Vertical,
                                        .color = sextant::Color{0.9f, 0.5f, 0.1f, 1.0f}, .alpha = 0.6f,
                                        .linewidth = 1.0f, .name = "area on ZX"});
        a6->legend();
        a6->set_title("planes");
    }

    savefig_and_check_memory(fig, "test_fill_between");
    fig->show(true);
}

// Step 36d-2: Axes::stack() and Plane2D::stack(): the four baselines, colors and
// names, a Vertical stepped stack with outlines, NaN carried up, a plane; each
// layer is an ordinary fill afterwards, edited alone in the Data panel.
static void test_stack() {
    using sextant::StackBaseline;
    using V = std::vector<double>;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Stacked areas"});
    fig->suptitle("Stacked areas (Axes::stack, Plane2D::stack)");

    // Five bumps, each a layer, peaking at different x.
    const std::size_t n = 120;
    V x(n);
    std::vector<V> ys(5, V(n));
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<double>(i) * 0.1;
        for (std::size_t k = 0; k < ys.size(); ++k) {
            const double c = 1.5 + 2.2 * static_cast<double>(k), w = 1.2 + 0.3 * static_cast<double>(k);
            ys[k][i] = 0.2 + (1.0 + 0.4 * static_cast<double>(k)) * std::exp(-(x[i] - c) * (x[i] - c) / (2.0 * w * w));
        }
    }
    const std::vector<std::string> names{"a", "b", "c", "d", "e"};

    const std::pair<StackBaseline, const char*> baselines[] = {
        {StackBaseline::Zero, "baseline Zero"},
        {StackBaseline::Sym, "baseline Sym"},
        {StackBaseline::Wiggle, "baseline Wiggle"},
        {StackBaseline::WeightedWiggle, "baseline WeightedWiggle (a streamgraph)"},
    };
    for (int b = 0; b < 4; ++b) {
        auto ax = fig->add_subplot(2, 3, b + 1);
        ax->stack(x, ys, {.baseline = baselines[b].first, .names = names, .alpha = 0.85f});
        if (b == 0) ax->legend();
        ax->set_title(baselines[b].second);
    }

    // Vertical, stepped, outlined, own colors; a NaN in layer 1 carries up.
    auto a5 = fig->add_subplot(2, 3, 5);
    {
        const V depth{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<V> w{{1, 2, 2, 3, 2, 1, 1, 2, 3, 2},
                         {2, 1, 1, 2, 3, 3, 2, 1, 1, 2},
                         {1, 1, 2, 1, 1, 2, 3, 3, 2, 1}};
        w[1][4] = std::nan("");
        a5->stack(depth, w, {.orientation = sextant::Orientation::Vertical,
                             .colors = {sextant::Color{0.85f, 0.35f, 0.2f, 1.0f}, sextant::Color{0.95f, 0.75f, 0.2f, 1.0f},
                                        sextant::Color{0.3f, 0.6f, 0.8f, 1.0f}},
                             .names = {"sand", "clay", "silt"}, .edgecolor = sextant::Color::Black,
                             .linewidth = 1.0f, .drawstyle = sextant::DrawStyle::StepsMid});
        a5->set_ylim(9.5, -0.5);
        a5->legend();
        a5->set_title("Vertical, steps_mid, outlined; NaN at depth 4 in clay");
    }

    // A stack on a plane.
    auto a6 = fig->add_subplot3d(2, 3, 6);
    {
        auto wall = a6->plane(sextant::PlaneOrientation::ZX, 0.0);
        wall->stack(x, std::span(ys).first(3), {.orientation = sextant::Orientation::Vertical, .baseline = StackBaseline::Sym,
                                                    .names = {"a", "b", "c"}, .alpha = 0.7f});
        a6->legend();
        a6->set_title("a Sym stack up z on a ZX plane");
    }

    savefig_and_check_memory(fig, "test_stack");
    fig->show(true);
}

// Steps 36e-1/36e-2: Axes::quiver() and Plane2D::quiver(): the automatic amp,
// equal aspect against free aspect colored by speed (with a colorbar), the
// three pivots, explicit amp and shape over a heatmap, a log axis, a plane
// colored by speed. Hover shows x, y, u, v (and c); the Data panel edits the
// arrows, their c and their appearance.
static void test_quiver() {
    using V = std::vector<double>;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Arrow fields"});
    fig->suptitle("Arrow fields (Axes::quiver, Plane2D::quiver)");

    // A vortex around the origin on a 15 x 15 grid.
    V gx, gy, gu, gv;
    for (int i = 0; i < 15; ++i)
        for (int j = 0; j < 15; ++j) {
            const double x = -2.0 + 4.0 * j / 14.0, y = -2.0 + 4.0 * i / 14.0;
            gx.push_back(x);
            gy.push_back(y);
            gu.push_back(-y);
            gv.push_back(x);
        }

    auto a1 = fig->add_subplot(2, 3, 1);
    a1->quiver(gx, gy, gu, gv, {.name = "(-y, x)"});
    a1->set_aspect(1.0);
    a1->legend();
    a1->set_title("automatic amp, equal aspect: arrows circle the origin");

    // Colored by speed (step 36e-2), at free aspect.
    V speed;
    for (std::size_t k = 0; k < gx.size(); ++k) speed.push_back(std::hypot(gu[k], gv[k]));
    auto a2 = fig->add_subplot(2, 3, 2);
    a2->quiver(gx, gy, gu, gv, speed,
               {.cmap = sextant::Colormap::Plasma, .vmax = 2.9f, .colorbar = true, .name = "speed"});
    a2->set_title("c = speed through plasma, free aspect: arrows follow the data units");

    // Pivots: the same vectors put down by tail, middle and tip, the anchors marked.
    auto a3 = fig->add_subplot(2, 3, 3);
    {
        const V px{0, 1, 2, 3, 4}, pu{0.6, 0.6, 0.0, -0.6, -0.6}, pv{0.0, 0.6, 0.8, 0.6, 0.0};
        const std::pair<sextant::QuiverPivot, const char*> pivots[] = {
            {sextant::QuiverPivot::Tail, "Tail"}, {sextant::QuiverPivot::Middle, "Middle"},
            {sextant::QuiverPivot::Tip, "Tip"}};
        for (int k = 0; k < 3; ++k) {
            const V py(px.size(), 2.0 * k);
            a3->quiver(px, py, pu, pv, {.amp = 1.0, .pivot = pivots[k].first, .name = pivots[k].second});
            a3->scatter(px, py, {.color = sextant::Color::Black, .size = 6.0f, .show_legend = false});
        }
        a3->set_aspect(1.0);
        a3->legend();
        a3->set_title("pivot: Tail, Middle, Tip (anchors in black)");
    }

    // The gradient of a bump over its heatmap: explicit amp, thin arrows.
    auto a4 = fig->add_subplot(2, 3, 4);
    {
        const int n = 40;
        V z(static_cast<std::size_t>(n * n));
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c) {
                const double x = -2.0 + 4.0 * (c + 0.5) / n, y = -2.0 + 4.0 * (r + 0.5) / n;
                z[static_cast<std::size_t>(r * n + c)] = std::exp(-(x * x + y * y));
            }
        a4->heatmap(z, n, n, {-2.0, 2.0}, {-2.0, 2.0}, {.cmap = sextant::Colormap::Viridis, .colorbar = true});
        V qx, qy, qu, qv;
        for (std::size_t k = 0; k < gx.size(); ++k) {
            const double e = std::exp(-(gx[k] * gx[k] + gy[k] * gy[k]));
            qx.push_back(gx[k]);
            qy.push_back(gy[k]);
            qu.push_back(2.0 * gx[k] * e);
            qv.push_back(2.0 * gy[k] * e);
        }
        a4->quiver(qx, qy, qu, qv, {.color = sextant::Color::White, .alpha = 0.9f, .amp = 0.35, .width = 1.2f,
                                    .head_width = 5.0f, .head_length = 6.0f, .name = "-grad"});
        a4->set_aspect(1.0);
        a4->set_title("-grad over the heatmap: amp 0.35, thin arrows");
    }

    // Log x: arrows placed on the scale; one reaching below 0 is left out.
    auto a5 = fig->add_subplot(2, 3, 5);
    {
        const V lx{1, 3, 10, 30, 100, 300}, ly{1, 2, 3, 4, 5, 6};
        const V lu{1, 3, 10, 30, -500, 300}, lv{0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
        a5->quiver(lx, ly, lu, lv, {.amp = 1.0, .name = "u = x (one reaching below 0)"});
        a5->set_xscale(sextant::Scale::Log);
        a5->legend();
        a5->set_title("log x: each arrow doubles x");
    }

    // A plane: the vortex on the floor.
    auto a6 = fig->add_subplot3d(2, 3, 6);
    {
        auto floor = a6->plane(sextant::PlaneOrientation::XY, 0.0);
        floor->quiver(gx, gy, gu, gv, speed, {.cmap = sextant::Colormap::Viridis, .vmax = 2.9f, .name = "vortex on XY"});
        auto wall = a6->plane(sextant::PlaneOrientation::ZX, 2.0);
        // A ZX plane's u is z and its v is x.
        wall->quiver(V{0.0, 0.0, 0.0}, V{-1.0, 0.0, 1.0}, V{0.5, 1.0, 1.5}, V{0.0, 0.0, 0.0},
                     {.amp = 1.0, .name = "up z on ZX"});
        a6->legend();
        a6->set_title("planes");
    }

    savefig_and_check_memory(fig, "test_quiver");
    fig->show(true);
}

// Step 36f: box zoom and navigation history. Turn on Edit > Navigate, then
// Shift-drag a box on each 2D panel (a thin one zooms one axis; Esc cancels),
// pan, scroll, orbit the 3D panel, and walk Edit > Back / Forward / Home. Home
// and the double-click go to the program's limits: panel 1's set_xlim(2, 8),
// panel 5's reversed x, the rest automatic.
static void test_navigation() {
    using V = std::vector<double>;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Navigation"});
    fig->suptitle("Edit > Navigate: Shift-drag zooms to a box; Edit > Back / Forward / Home");

    V x(400), s(400), e(400), c(400);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 10.0 * static_cast<double>(i) / 399.0;
        s[i] = std::sin(3.0 * x[i]) + 0.2 * std::sin(17.0 * x[i]);
        e[i] = std::exp(x[i]);
        c[i] = std::cos(x[i]);
    }

    auto a1 = fig->add_subplot(2, 3, 1);
    a1->line(x, s, {.name = "sin"}).set_xlim(2.0, 8.0);
    a1->set_title("set_xlim(2, 8): Home comes back here");

    auto a2 = fig->add_subplot(2, 3, 2);
    a2->line(x, e, {.name = "exp"}).set_yscale(sextant::Scale::Log);
    a2->set_title("log y: a box zooms in decades");

    auto a3 = fig->add_subplot(2, 3, 3);
    a3->line(x, s, {.name = "sin"}).set_ytitle("sin");
    a3->twinx()->line(x, e, {.color = sextant::Color::Orange, .name = "exp"}).set_ytitle("exp");
    a3->set_title("twinx: both y axes zoom to the box");

    auto a4 = fig->add_subplot(2, 3, 4);
    {
        V cx, cy;
        for (int i = 0; i <= 100; ++i) {
            const double t = 2.0 * 3.14159265358979 * i / 100.0;
            cx.push_back(std::cos(t));
            cy.push_back(std::sin(t));
        }
        a4->line(cx, cy, {.name = "circle"});
        a4->set_aspect(1.0);
        a4->set_title("equal aspect: the box grows to the frame's shape");
    }

    auto a5 = fig->add_subplot(2, 3, 5);
    a5->scatter(x, c, {.name = "cos"}).set_xlim(10.0, 0.0);
    a5->set_title("reversed x: stays reversed");

    auto a6 = fig->add_subplot3d(2, 3, 6);
    {
        V gu, gv, gz;
        for (int i = 0; i < 30; ++i) gu.push_back(-3.0 + 6.0 * i / 29.0);
        gv = gu;
        for (double v : gv)
            for (double u : gu) gz.push_back(std::sin(u) * std::cos(v));
        a6->surface(sextant::PlaneOrientation::XY, gu, gv, gz, {.alpha = 0.9f});
        a6->set_view(-50.0, 28.0);
        a6->set_title("3D: Back undoes an orbit; Home is set_view(-50, 28)");
    }

    fig->show(true);
}

int main() {
    // test_navigation();
    // test_quiver();
    // test_stack();
    // test_fill_between();
    // test_reference_lines();
    // test_bar_orientation();
    // test_line_steps();
    // test_marker_sizes();
    // test_marker_shapes();
    // test_tick_style();
    // test_layout_independent();
    // test_layout_aligned();
    // test_aspect();
    // test_twins();
    // test_shared_axes();
    // test_scales();
    // test_axis_position3d();
    // test_axis_position();
    test_axes_gallery();
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
    // test_wait_and_run();
    // test_refresh_before_show();
    // test_live_refresh();
    // test_grid_toggle();
    // test_widget_panel();
    // test_theming();
    // test_frame_resize();
    // test_mouse_hint();
    // test_data_panel();
    // test_events();
}
