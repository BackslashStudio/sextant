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
#include <stdexcept>
#include <iostream>

// 1. Plot gallery — every basic plot type in one subplot grid.
// all of them at once at a bigger scale than any single-purpose demo needed.
static void test_plot_gallery() {
    constexpr int N = 200;
    std::vector<double> x(N), y_sin(N), y_cos(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 2.0 * M_PI / N;
        y_sin[i] = std::sin(x[i]);
        y_cos[i] = std::cos(x[i]);
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

    // Scatter_z ring: continuous-value series colored by z, own colorbar,
    // coexisting with the same A/B classes' legend on this one cell.
    constexpr int M = 80;
    std::vector<double> ring_x(M), ring_y(M), ring_z(M);
    for (int i = 0; i < M; ++i) {
        double t = i * 2.0 * M_PI / M;
        ring_x[i] = 3.0 * std::cos(t);
        ring_y[i] = 3.0 * std::sin(t);
        ring_z[i] = t;
    }

    // Error-bar cell: a measured curve whose whisker spans an *asymmetric*
    // min/max range with a variance box inside it, over a coarser bar series
    // with a symmetric range. The line's box is deliberately much narrower
    // than its whisker, which is what makes the two halves distinguishable.
    constexpr int E = 10;
    std::vector<double> err_x(E), err_y(E), err_min(E), err_max(E), err_var(E);
    for (int i = 0; i < E; ++i) {
        err_x[i]   = i * 0.7;
        err_y[i]   = 4.0 + 2.5 * std::sin(err_x[i]);
        err_min[i] = err_y[i] - (0.25 + 0.05 * i);   // absolute coordinates,
        err_max[i] = err_y[i] + (0.60 + 0.10 * i);   // not magnitudes
        err_var[i] = 0.12 + 0.02 * i;
    }
    const std::vector<double> err_bx  = {0.7, 2.8, 4.9};
    const std::vector<double> err_bh  = {3.0, 5.5, 2.2};
    const std::vector<double> err_bmn = {2.4, 4.6, 1.8};
    const std::vector<double> err_bmx = {3.6, 6.4, 2.6};
    const std::vector<double> err_bvr = {0.3, 0.45, 0.2};

    // Scatter_z error bars are exercised on the scatter_z cell below: a 2D
    // box (xvar wide, yvar tall) is the shape only the scatter kinds draw.
    std::vector<double> ring_xv(M), ring_yv(M);
    for (int i = 0; i < M; ++i) { ring_xv[i] = 0.25; ring_yv[i] = 0.4; }

    auto fig = sextant::Figure::create({.width = 1800, .height = 1000, .title = "Plot gallery", .supersample = 2});

    fig->add_subplot(3, 4, 1)
            ->line(x, y_sin, {.color = sextant::Color::Blue, .linewidth = 4.0f, .label = "sin(x)"})
            .line(x, y_cos, {
                      .color = sextant::Color::Red, .linewidth = 2.0f,
                      .linestyle = sextant::LineStyle::Dashed, .label = "cos(x)"
                  })
            .set_title("Line").legend().grid();

    fig->add_subplot(3, 4, 2)
            ->scatter(xa, ya, {.color = sextant::Color::Blue, .marker = sextant::MarkerStyle::Circle, .label = "A"})
            .scatter(xb, yb, {.color = sextant::Color::Orange, .marker = sextant::MarkerStyle::Diamond, .label = "B"})
            .set_title("Scatter").legend().grid();

    fig->add_subplot(3, 4, 3)
            ->bar(months, sales, {.color = sextant::Color::Blue, .edgecolor = sextant::Color::Black})
            .set_title("Bar").grid();

    fig->add_subplot(3, 4, 4)
            ->bar(cats, deltas, {.color = sextant::Color::Cyan, .edgecolor = sextant::Color::Black})
            .set_title("Bar (diverging)").grid();

    fig->add_subplot(3, 4, 5)
            ->hist(hist_data, 30, {.color = sextant::Color::Purple, .width = 1.0f}, {.density = true})
            .set_title("Histogram").grid();

    fig->add_subplot(3, 4, 6)
            ->hist(cum_data, 25, {.color = sextant::Color::Green, .width = 1.0f}, {.density = true, .cumulative = true})
            .set_title("Histogram (cumulative)").grid();

    // Contours on the gaussian, labelled, so the whole feature is on show in
    // one cell. White at 1.5px because black at 1px vanishes into viridis's
    // dark end. Pan and zoom this one: the geometry is traced once and only
    // re-projected per frame, so it must stay glued to the colours under it.
    fig->add_subplot(3, 4, 7)
            ->heatmap(gauss, R, C, {.cmap = sextant::Colormap::Viridis, .vmin = 0.0f, .vmax = 1.0f, .colorbar = true,
                                    .contours = {0.2, 0.4, 0.6, 0.8},
                                    .contour_color = {1.0f, 1.0f, 1.0f, 0.9f},
                                    .contour_linewidth = 1.5f,
                                    .contour_labels = true})
            .set_title("Heatmap + contours");

    fig->add_subplot(3, 4, 8)
            ->heatmap(checker, R, C, {.vmin = 0.0f, .vmax = 1.0f, .origin = "upper"})
            .set_title("Heatmap (origin=upper)");

    fig->add_subplot(3, 4, 9)
            ->line(pcurve_x, pcurve_y, {.color = sextant::Color::Red, .linewidth = 2.0f, .label = "fit"})
            .scatter(ppts_x, ppts_y, {.color = sextant::Color::Blue, .size = 14.0f, .label = "samples", .alpha = 0.6f})
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

    fig->add_subplot(3, 4, 11)
            ->scatter(xa, ya, {.color = sextant::Color::Blue, .label = "A"})
            .scatter(xb, yb, {.color = sextant::Color::Orange, .label = "B"})
            .scatter_z(ring_x, ring_y, ring_z, {
                           .cmap = sextant::Colormap::Viridis, .size = 14.0f,
                           .vmin = 0.0f, .vmax = static_cast<float>(2.0 * M_PI), .colorbar = true,
                           // The 2D variance box only the scatter
                           // kinds draw — xvar wide and yvar tall, no whisker.
                           .errorbar = {.yvar = ring_yv, .xvar = ring_xv,
                                        .linewidth = 0.75f}
                       })
            .set_title("scatter_z").legend().grid();

    // Error bars, both y-direction kinds in one cell: the line's whisker is
    // asymmetric about its point with a narrow variance box inside it, the
    // bar's is symmetric, so a regression in either half shows up here.
    fig->add_subplot(3, 4, 12)
            ->line(err_x, err_y, {
                       .color = sextant::Color::Blue, .linewidth = 2.0f, .label = "measured",
                       .errorbar = {.ymin = err_min, .ymax = err_max, .yvar = err_var,
                                    .color = sextant::Color::Gray, .capsize = 8.0f}
                   })
            .bar(err_bx, err_bh, {
                     .color = sextant::Color::Orange, .alpha = 0.5f, .label = "binned",
                     .errorbar = {.ymin = err_bmn, .ymax = err_bmx, .yvar = err_bvr,
                                  .linewidth = 1.5f}
                 })
            .set_title("Error bars").legend().grid();

    fig->show(true);
    fig->savefig("test_plot_gallery.png");
    fig->savefig("test_plot_gallery.svg");
    printf("Saved: test_plot_gallery.png, test_plot_gallery.svg\n");
}

// 2. Headless export — multi-axes figure saved to PNG and SVG.
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
            ->line(x, y_sin, {.color = sextant::Color::Blue, .linewidth = 2.0f, .label = "sin(x)"})
            .set_title("Line").set_xtitle("x").set_ytitle("y").legend().grid();
    fig->add_subplot(1, 3, 2)
            ->bar(months, sales, {.color = sextant::Color::Cyan, .edgecolor = sextant::Color::Black})
            .set_title("Bar").set_xtitle("month").set_ytitle("units").grid();
    // The same contours as the gallery cell, reached through the headless
    // path: the trace, the projection and the label placement all have to work
    // with no window and no GL context of their own.
    fig->add_subplot(1, 3, 3)
            ->heatmap(gauss, R, C, {.cmap = sextant::Colormap::Viridis, .vmin = 0.0f, .vmax = 1.0f, .colorbar = true,
                                    .contours = {0.2, 0.4, 0.6, 0.8},
                                    .contour_color = {1.0f, 1.0f, 1.0f, 0.9f},
                                    .contour_linewidth = 1.5f,
                                    .contour_labels = true})
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
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f, .label = "sin(x)"})
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

// 5. Live refresh — mutate Axes from the main thread while the background
// render thread is running, exercising the pattern that used to race.
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
    // Deliberately no titles/xtitles/ytitles anywhere: the layout should
    // reserve nothing for them, which is what the fixed margins could only
    // approximate and only when tight_layout was asked for.

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

// 12. Frame-driven resize -- the window grows so that the *plot frame* becomes
// a requested size, with the menu bar and Controls column added on top of the
// derived figure size. Interactive; the headless half is asserted in
// sextant_layout_test.
static void test_frame_resize() {
    constexpr int N = 120;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 4.0 * M_PI / N;
        y[i] = std::sin(x[i]) * std::exp(-x[i] / 12.0);
    }

    auto fig = sextant::Figure::create({.width = 700, .height = 500, .title = "Frame resize"});
    fig->axes()->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f,
                             .label = "damped sine"})
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
    // Held in named variables, not passed as temporaries: each window stays
    // open for as long as its Figure is alive, so all three coexist.
    auto dark = make_fig(sextant::PanelTheme::Dark, "Theming: Dark");
    auto light = make_fig(sextant::PanelTheme::Light, "Theming: Light");
    auto classic = make_fig(sextant::PanelTheme::Classic, "Theming: Classic");
    dark->show(false);
    light->show(false);
    classic->show(true);
}

// 9. Mouse hint -- hover tooltip over a 1x4 grid (line with custom labels and
// error bars, scatter_z, heatmap, custom-hint scatter). Interactive-only; the
// checklist is in spec_test.md.
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

    // Dedicated custom-hint subplot: every point has its own name, unlike
    // the Line subplot above where only two of sixty points are labeled.
    const std::vector<double> store_x = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<double> store_y = {4, 7, 2, 9, 5, 3, 8, 6};
    const std::vector<std::string> store_hints = {
        "Store A", "Store B", "Store C", "Store D",
        "Store E", "Store F", "Store G", "Store H",
    };

    // The Line cell also carries error bars, so its hover text exercises both
    // halves alongside a custom hint label -- all three have to coexist on one
    // tooltip.
    std::vector<double> hint_min(N), hint_max(N), hint_var(N);
    for (int i = 0; i < N; ++i) {
        const auto j = static_cast<std::size_t>(i);
        hint_min[j] = y[j] - (0.05 + 0.002 * i);
        hint_max[j] = y[j] + (0.15 + 0.004 * i);
        hint_var[j] = 0.04;
    }

    auto fig = sextant::Figure::create({.width = 2000, .height = 500, .title = "Mouse hint"});
    fig->add_subplot(1, 4, 1)
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f,
                          .errorbar = {.ymin = hint_min, .ymax = hint_max,
                                       .yvar = hint_var},
                          .hint_labels = line_hints})
            .set_title("Line").set_xtitle("x").set_ytitle("y");
    fig->add_subplot(1, 4, 2)
            ->scatter_z(zx, zy, zz, {.size = 30.0f, .colorbar = true})
            .set_title("Scatter Z");
    fig->add_subplot(1, 4, 3)
            ->heatmap(img, ROWS, COLS, {.colorbar = true})
            .set_title("Heatmap");
    fig->add_subplot(1, 4, 4)
            ->scatter(store_x, store_y, {.color = sextant::Color::Orange, .size = 24.0f, .hint_labels = store_hints})
            .set_title("Custom hint").set_xlim(0, 9).set_ylim(0, 10);
    fig->show(true);
}

// 10. Data panel -- editable tables for the current axes' plot objects.
// Interactive-only; the checklist is in spec_test.md.
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

    // Sparse samples drawn on the SAME axes as the sine line, so one axes
    // carries two plot objects of different kinds — both sit at plot_index 0
    // in their own per-kind vector, so only PlotKind tells their edits apart.
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
            ->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f, .label = "Sine"})
            .scatter(px, py, {.color = sextant::Color::Red, .size = 24.0f, .label = "Samples"})
            .set_title("Line + scatter").set_xtitle("x").set_ytitle("y").legend();
    fig->add_subplot(2, 2, 2)
            ->scatter_z(zx, zy, zz, {.size = 30.0f, .colorbar = true})
            .set_title("Scatter Z");
    fig->add_subplot(2, 2, 3)
            ->hist(samples, 12, {.color = sextant::Color::Orange, .width = 1.0f})
            .set_title("Hist (a BarPlot)");
    fig->add_subplot(2, 2, 4)
            ->heatmap(img, ROWS, COLS, {.colorbar = true})
            .set_title("Heatmap 8x6");
    fig->show(false);

    // Wider than one ImGui table can hold, so the grid must fall back to
    // column paging. `fig` above is deliberately still alive, so both Data
    // panels are on screen at once.
    constexpr int WROWS = 12, WCOLS = 600;
    std::vector<float> wide(WROWS * WCOLS);
    for (int r = 0; r < WROWS; ++r)
        for (int c = 0; c < WCOLS; ++c)
            wide[r * WCOLS + c] = static_cast<float>(c) / static_cast<float>(WCOLS);

    auto fig2 = sextant::Figure::create({.width = 1400, .height = 700, .title = "Data panel: wide heatmap"});
    fig2->axes()->heatmap(wide, WROWS, WCOLS, {.colorbar = true}).set_title("Heatmap 12x600");
    fig2->show(true);
}

// Every cosmetic option, rendered twice: once all-default and once with each
// field set to a distinctly non-default value. The gallery's byte-identity
// check cannot see these, since the defaults reproduce the old hardcoded look
// exactly, so this is what proves they reach both outputs. Headless.
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
        // Supersample=1 so the raster output is not box-filtered — keeps any
        // manual inspection of these unambiguous, the same reasoning as the
        // exact-equivalence check.
        auto fig = sextant::Figure::create({.width = 900, .height = 500,
                                .title = "Style options", .supersample = 1});
        fig->suptitle("Suptitle text");

        auto ax = fig->add_subplot(1, 2, 1);
        ax->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f, .label = "sin(x)"})
           .set_title("Titled").set_xtitle("x axis").set_ytitle("y axis")
           .legend().grid();

        auto ax2 = fig->add_subplot(1, 2, 2);
        ax2->heatmap(heat, R, C, {.vmin = 0.0f, .vmax = 1.0f, .colorbar = true})
            .set_title("Heat");

        if (styled) {
            sextant::SuptitleOptions sup;
            sup.fontsize = 28.0f;
            sup.color    = {0.0f, 0.2f, 0.9f, 1.0f};      // blue
            sup.align    = sextant::HAlign::Right;
            sup.offset_x = -20.0f;
            sup.offset_y = 4.0f;
            fig->set_suptitle_style(sup);

            // Note this also carries the three title font sizes: set_axes_style
            // replaces the whole struct, so it must (documented in style.h).
            sextant::AxesStyle as;
            as.spine_color     = {0.9f, 0.1f, 0.1f, 1.0f};
            as.spine_linewidth = 3.0f;
            as.tick_color      = {0.1f, 0.6f, 0.1f, 1.0f};
            as.tick_length     = 12.0f;
            as.label_color     = {0.9f, 0.0f, 0.0f, 1.0f};  // per-tick labels
            as.label_fontsize  = 17.0f;
            as.title_fontsize  = 30.0f;
            as.xtitle_fontsize = 24.0f;
            as.ytitle_fontsize = 24.0f;
            ax->set_axes_style(as);

            ax->grid(true, {.color     = {0.0f, 0.6f, 0.6f, 1.0f},   // teal
                            .linestyle = sextant::LineStyle::Dashed,
                            .linewidth = 2.0f});

            sextant::LegendOptions lo;
            lo.fontsize         = 16.0f;
            lo.text_color       = {0.0f, 0.5f, 0.0f, 1.0f};   // green
            lo.frame_color      = {0.1f, 0.1f, 0.1f, 0.9f};
            lo.border_color     = {1.0f, 0.5f, 0.0f, 1.0f};
            lo.border_linewidth = 3.0f;
            ax->legend(lo);

            sextant::ColorbarOptions cb;
            cb.fontsize         = 16.0f;
            cb.text_color       = {1.0f, 0.4f, 0.0f, 1.0f};   // orange
            cb.border_color     = {0.5f, 0.0f, 0.5f, 1.0f};   // purple
            cb.border_linewidth = 3.0f;
            ax2->set_colorbar_style(cb);
        }

        fig->savefig(stem + ".png");
        fig->savefig(stem + ".svg");
    };

    render(false, "test_style_default");
    render(true,  "test_style_custom");
    printf("Saved: test_style_{default,custom}.{png,svg}\n");
}

// -------------------------------------------------------------------------
int main() {
    test_plot_gallery();
    // test_style_options();
    // test_suptitle_layout();
    // test_savefig();
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
