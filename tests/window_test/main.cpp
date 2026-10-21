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

// Mathtext gallery: every construct drawn so far, through text(). Each item
// is its source (grey, plain: no `$`) over the math it draws, so it can be
// compared with matplotlib's. One cell per kind; the last puts math where
// labels go (title, axis titles, legend, a rotated, a boxed and a multi-line
// text). What to look for: nothing overlaps its neighbours; fraction bars,
// roots, accents and drawn delimiters meet the glyphs cleanly; limits are
// centred over and under their operator; the PNG and SVG agree.
// Two pages: the text font, then a math font (FigureOptions::math_fontset):
// Stix where installed, else a math font found as Custom (DejaVu Math TeX
// Gyre, Noto Sans Math), else Cm, which no machine here has -- that page then
// says so in its title and the console shows the one warning.
static void mathtext_gallery_page(sextant::MathFontset set, const std::string& path, const std::string& name,
                                  const std::string& stem) {
    auto fig = sextant::Figure::create({
        .width = 1800, .height = 1100,
        .title = "sextant — mathtext (" + name + ")",
        .math_fontset = set, .math_font_path = path
    });
    fig->suptitle("Mathtext in " + name + ": source above, drawn below");

    struct Cell {
        const char* title;
        std::vector<const char*> items;
    };
    const Cell cells[] = {
        {"Scripts, symbols, fonts", {
            "x^2 + y_i^{n+1} - z_{i,j}",
            "\\alpha\\beta\\gamma\\epsilon\\phi\\;\\Gamma\\Delta\\Omega\\;\\hbar\\ell\\infty",
            "a \\leq b \\neq c \\approx d \\in A \\subseteq B",
            "\\sin(\\omega t) \\rightarrow \\infty,\\ \\log_2 n,\\ \\pm\\times\\cdot",
            "\\mathrm{d}x\\,\\mathit{AB2}\\,\\text{rate}_{\\text{in}},\\ f'(x),\\ \\nabla\\cdot F",
            "\\mathbf{F} = m\\mathbf{a},\\ \\mathbf{x}_{\\mathbf{1}}^2,\\ \\mathsf{sans1}\\,\\mathtt{mono1}",
            "\\mathcal{L}\\,\\mathcal{H}\\,\\mathscr{E}\\quad\\mathbb{R}^n\\,\\mathbb{Z}\\,\\mathbb{1}\\quad"
            "\\mathfrak{g}\\,\\mathfrak{R}",
        }},
        {"Fractions, roots, rules", {
            "\\frac{a+b}{c}\\quad\\dfrac{1}{2}\\quad\\tfrac{1}{2}\\quad\\frac12",
            "\\frac{\\frac{1}{x}+1}{y}\\quad e^{-\\frac{(x-\\mu)^2}{2\\sigma^2}}",
            "\\sqrt{2}\\quad\\sqrt[3]{x}\\quad\\sqrt{\\frac{a}{b}}\\quad\\sqrt{x_i^2+y}",
            "\\overline{AB}\\quad\\underline{xyz}\\quad\\overline{\\overline{z}}\\quad x^{\\frac{1}{2}}",
            "\\frac{1}{\\sigma\\sqrt{2\\pi}}",
        }},
        {"Accents", {
            "\\hat{x}\\,\\bar{x}\\,\\tilde{a}\\,\\dot{x}\\,\\ddot{y}\\,\\vec{v}",
            "\\acute{e}\\,\\grave{a}\\,\\breve{u}\\,\\check{c}\\,\\mathring{A}",
            "\\hat{X}\\,\\bar{F}\\,\\hat{\\imath}\\,\\vec{\\jmath}\\,\\vec{x}^2\\,\\hat{\\theta}_k",
            "\\widehat{xyz}\\quad\\widetilde{abc}\\quad\\widehat{i}",
            "\\overrightarrow{AB}\\quad\\overleftarrow{PQ}",
        }},
        {"Delimiters", {
            "\\left(\\frac{a}{b}\\right)\\left[\\frac{1}{x}\\right]\\left\\{\\frac{\\frac{a}{b}}{c}\\right\\}"
            "\\left(x\\right)",
            "\\left|\\frac{a}{b}\\right|\\left\\|\\frac{a}{b}\\right\\|\\left\\langle\\frac{a}{b}\\right\\rangle",
            "\\left\\lfloor\\frac{a}{b}\\right\\rfloor\\left\\lceil\\frac{a}{b}\\right\\rceil",
            "\\bigl(\\Bigl(\\biggl(\\Biggl(x\\Biggr)\\biggr)\\Bigr)\\bigr)\\quad\\Bigl[\\bigl|x\\bigr|\\Bigr]",
            "\\binom{n}{k}\\quad\\left\\{x\\middle|\\frac{x}{2}>0\\right\\}\\quad\\left/\\frac{a}{b}\\right\\backslash",
            "\\left.\\frac{df}{dx}\\right|_{x=0}\\quad\\left(\\sum_{i=1}^{n}x_i\\right)^2",
        }},
        {"Operators and limits", {
            "\\sum_{i=0}^{n} x_i\\quad\\prod_{k=1}^{N} a_k\\quad\\coprod_j",
            "\\int_0^1 f\\,dx\\quad\\iint_D g\\quad\\oint_C\\vec{F}\\cdot d\\vec{r}",
            "\\lim_{x\\to 0}\\frac{\\sin x}{x}\\quad\\max_x f\\quad\\sup_{n} a_n",
            "\\prod_{\\substack{0<i<n\\\\ i\\neq j}} a_i\\quad\\bigcup_k A_k\\quad\\bigoplus_k V_k\\quad x^{\\sum_i y}",
            "\\overset{!}{=}\\quad A\\stackrel{f}{\\to}B\\quad\\underset{x}{\\operatorname{argmax}}\\,g",
        }},
    };

    sextant::AxesStyle bare;
    bare.show_xticks = bare.show_yticks = false;
    bare.spine_bottom = bare.spine_left = bare.spine_top = bare.spine_right = false;
    sextant::TextOptions source;
    source.fontsize = 10.0f;
    source.color = {0.45f, 0.45f, 0.45f, 1.0f};
    source.va = sextant::VAlign::Top;
    sextant::TextOptions math;
    math.fontsize = 20.0f;
    math.va = sextant::VAlign::Top;
    math.dy = -16.0f;
    int index = 1;
    for (const Cell& c: cells) {
        auto ax = fig->add_subplot(2, 3, index++);
        ax->set_axes_style(bare).set_title(c.title);
        const double step = 0.95 / static_cast<double>(c.items.size());
        for (std::size_t i = 0; i < c.items.size(); ++i) {
            const double y = 0.98 - step * static_cast<double>(i);
            ax->text(c.items[i], sextant::Pos::fraction(0.02), sextant::Pos::fraction(y), source);
            ax->text(std::string("$") + c.items[i] + "$", sextant::Pos::fraction(0.02),
                     sextant::Pos::fraction(y), math);
        }
    }

    // Math where labels go.
    std::vector<double> t(60), s(60), r(60);
    for (int i = 0; i < 60; ++i) {
        t[i] = i / 59.0;
        s[i] = std::sqrt(t[i]);
        r[i] = t[i] * t[i];
    }
    auto ax = fig->add_subplot(2, 3, 6);
    ax->line(t, s, {.name = "$\\sqrt{t}$"})
        .line(t, r, {.name = "$\\overline{x}_n = \\frac{1}{n}\\sum_i x_i$"})
        .legend()
        .set_title("$\\sum_{k=1}^{\\infty}\\frac{1}{k^2} = \\frac{\\pi^2}{6}$")
        .set_xtitle("$\\int_0^t v\\,d\\tau$ [m]")
        .set_ytitle("$\\left|\\hat{H}(\\omega)\\right|$");
    sextant::TextOptions turned;
    turned.fontsize = 18.0f;
    turned.rotation = 25.0f;
    ax->text("$\\frac{1}{\\sqrt{n}}\\sum_{i=1}^{n}X_i$", sextant::Pos::fraction(0.55), sextant::Pos::fraction(0.25), turned);
    sextant::TextOptions boxed;
    boxed.fontsize = 16.0f;
    boxed.edge_linewidth = 1.0f;
    boxed.background = {1.0f, 1.0f, 0.85f, 1.0f};
    ax->text("$\\hat{\\beta} = \\left(X^TX\\right)^{-1}X^Ty$", sextant::Pos::fraction(0.05), sextant::Pos::fraction(0.62), boxed);
    sextant::TextOptions lines;
    lines.fontsize = 14.0f;
    ax->text("$\\lim_{n\\to\\infty}a_n$\nplain line\n$\\frac{1}{2}$ last", sextant::Pos::fraction(0.62),
             sextant::Pos::fraction(0.40), lines);

    savefig_and_check_memory(fig, stem);
    fig->show(true);
}

static void test_mathtext_gallery() {
    mathtext_gallery_page(sextant::MathFontset::Text, "", "the text font", "test_mathtext_gallery");

    // The math font: the first choice this machine has, found by creating a
    // figure with each until one sends no message.
    struct Choice {
        sextant::MathFontset set;
        const char* path;
        const char* name;
    };
    const Choice choices[] = {
        {sextant::MathFontset::Stix, "", "Stix"},
        {sextant::MathFontset::Custom, "C:/Windows/Fonts/DejaVuMathTeXGyre.ttf", "DejaVu Math TeX Gyre"},
        {sextant::MathFontset::Custom, "/usr/share/fonts/TTF/DejaVuMathTeXGyre.ttf", "DejaVu Math TeX Gyre"},
        {sextant::MathFontset::Custom, "/usr/share/fonts/noto/NotoSansMath-Regular.ttf", "Noto Sans Math"},
    };
    bool warned = false;
    sextant::Figure::set_message_handler([&](std::string_view) { warned = true; });
    const Choice* pick = nullptr;
    for (const Choice& c: choices) {
        warned = false;
        (void) sextant::Figure::create({.math_fontset = c.set, .math_font_path = c.path});
        if (!warned) {
            pick = &c;
            break;
        }
    }
    sextant::Figure::set_message_handler(nullptr);
    if (pick)
        mathtext_gallery_page(pick->set, pick->path, pick->name, "test_mathtext_gallery_math");
    else
        mathtext_gallery_page(sextant::MathFontset::Cm, "", "Cm (not installed: the text font)",
                              "test_mathtext_gallery_math");
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
            .set_xtitle("$\\hat{\\theta}$ [rad]").set_ytitle("$\\left(\\frac{\\sqrt{2}}{2}\\right)\\overline{y}$")
            .set_title("Line").legend().grid();

    // C starts hidden: tick Visible on its Data-panel tab and the
    // limits widen to x = 4, and the legend gains its key.
    std::vector<double> xc(xb);
    for (double& v : xc) v += 5.0;
    fig->add_subplot(3, 4, 2)
            ->scatter(xa, ya, {.color = sextant::Color::Blue, .marker = sextant::MarkerStyle::Circle, .name = "A"})
            .scatter(xb, yb, {.color = sextant::Color::Orange, .marker = sextant::MarkerStyle::Diamond, .name = "B"})
            .scatter(xc, yb, {
                         .color = sextant::Color::Green, .marker = sextant::MarkerStyle::Square,
                         .name = "C (hidden)", .visible = false
                     })
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

// line3d gallery: four cells, each judged by eye.
static void test_line3d() {
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
    savefig_and_check_memory(fig, "test_line3d");
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
static void part_errorbar3d() {
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
    savefig_and_check_memory(fig, "part_errorbar3d");
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
static void test_surface_tri() {
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
    savefig_and_check_memory(fig, "test_surface_tri");
}

// 3. Non-blocking show — window runs in background while main thread continues
static void part_nonblocking() {
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
    savefig_and_check_memory(fig, "test_show_nonblocking");
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
static void part_wait_and_run() {
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
static void part_refresh_before_show() {
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
static void part_spans() {
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

    fig->savefig("part_spans.png");
    fig->savefig("part_spans.svg");
    printf("Saved: part_spans.png/.svg\n");

    fig->show(true);
}

// -------------------------------------------------------------------------
// Axis scales: log, log-log, symlog, minor ticks and grid, tick
// formats. Interactive: pan and zoom move whole decades on a log axis and keep
// the point under the cursor; the hover readout shows data values; the
// Cosmetic panel's Ticks section switches each axis' scale (disabled on the
// heatmap cell), its minor marks and its format (red while not a %f/%e/%g).
static void part_scales() {
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

    savefig_and_check_memory(fig, "part_scales");
    fig->show(true);
}

// -------------------------------------------------------------------------
// Shared axes: sharex down the left column, sharey across the
// top row, and a heatmap sharing x below. Interactive: pan or zoom one cell of
// a group and the others follow on that axis only; the inner cells' tick labels
// are hidden (their room given back), the bottom row's shown; cell 1's legend
// and cell 5's colorbar narrow all three frames of the column alike; the Cosmetic
// panel says "X shared with ..." under Limits, its Labels X/Y combos override
// the hiding, and a scale set on one member is the group's (disabled on the
// heatmap's group).
static void part_shared() {
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

    savefig_and_check_memory(fig, "part_shared");
    fig->show(true);
}

// twinx()/twiny(). Check: each twin's axis on its own edge
// (cell 1's host asks for its y axis on the right and is ignored); the
// legend lists both layers in call order; drag, wheel and double-click move
// every layer; the menu bar's Host / Twin selector beside the subplot one; cell 3's
// twin labels hide toward cell 4's twin, which shares its (log) y.
static void part_twins() {
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

    savefig_and_check_memory(fig, "part_twins");
    fig->show(true);
}

// set_aspect(). Check: each circle stays round while the
// window resizes, pans and zooms; cell 1's frame shrinks and re-centres with
// its title, ticks and legend (leaving its column's alignment); cell 2 keeps
// its frame and widens its limits; cell 3 is log-log in decades; cell 4 is a
// Datalim axes sharing x with cell 1, so its y adjusts and its x pans with
// cell 1's. The Ticks section's Aspect / Ratio / Adjust rows (Datalim greyed
// when both axes are shared).
static void part_aspect() {
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

    savefig_and_check_memory(fig, "part_aspect");
    fig->show(true);
}

// Step 36c-1: every marker shape, filled / outlined / hollow; the line markers
// at their default and wider strokes; legend keys; scatter_z, 3D and a plane.
static void part_marker_shapes() {
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

    savefig_and_check_memory(fig, "part_marker_shapes");
    fig->show(true);
}

// Step 36c-2: per-point marker sizes -- a bubble chart, hidden points, a
// plane and a 3D cloud; the Data panel's size column.
static void part_marker_sizes() {
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

    savefig_and_check_memory(fig, "part_marker_sizes");
    fig->show(true);
}

// Step drawstyles (LineOptions::drawstyle): the four modes over the same points,
// a log axis with NaN gaps and error bars, a looped staircase, and a plane.
static void part_line_steps() {
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

    savefig_and_check_memory(fig, "part_line_steps");
    fig->show(true);
}

// Horizontal bars and bar bases (BarOptions::orientation, BarOptions::bottom):
// stacked, a waterfall, lying bars with error bars, a horizontal hist, log x,
// and a plane. The Data panel's Orientation combo flips any of them.
static void part_bar_orientation() {
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

    savefig_and_check_memory(fig, "part_bar_orientation");
    fig->show(true);
}

// Step 36c-5: Axes::axline()/axspan(), both orientations, frame fractions,
// dashes, outlines, legend keys, call order, twins, log and live movement.
static void part_reference_lines() {
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

    savefig_and_check_memory(fig, "part_reference_lines");
    fig->show(true);
}

// Step 36d-1: Axes::fill_between() and Plane2D::fill_between(): areas under a
// curve and bands, exclude and interpolate, steps, the Vertical orientation
// with an outline, log axes and NaN gaps, planes; the Data panel edits them.
static void part_fill_between() {
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

    savefig_and_check_memory(fig, "part_fill_between");
    fig->show(true);
}

// Step 36d-2: Axes::stack() and Plane2D::stack(): the four baselines, colors and
// names, a Vertical stepped stack with outlines, NaN carried up, a plane; each
// layer is an ordinary fill afterwards, edited alone in the Data panel.
static void part_stack() {
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

    savefig_and_check_memory(fig, "part_stack");
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

    // Colored by speed, at free aspect.
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

// -------------------------------------------------------------------------
// Interactive events. Click, scroll, press keys and resize the window;
// each event is printed on this thread (the one blocked in wait_closed()). The
// left figure is 2D, the right one a 3D scene with a Plane2D; Edit > Navigate
// shows `consumed`, and a click on a line point, bar, marker or heatmap cell adds a
// `pick` line naming the object and element. Type in a panel field to see keys
// stay quiet. Click an object on the first two plots and press x to remove it by its
// ObjectId; a second x says it was not found. Close the
// window to finish.
static void test_events() {
    const char* consumed_names[] = {"none", "select", "navigate", "grid-drag"};
    const char* kinds[] = {"close", "down", "up", "move", "scroll", "key-down", "key-up", "resize", "pick"};
    const char* pick_names[] = {"none", "line", "scatter", "scatter_z", "bar", "heatmap",
                                "bar3d", "surface", "surface_tri", "scatter3d", "line3d",
                                "quiver", "fill"};

    auto fig = sextant::Figure::create({.width = 1350, .height = 450, .title = "events"});
    constexpr int N = 60;
    std::vector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) {
        x[i] = i * 0.1;
        y[i] = std::sin(x[i]);
    }
    auto first = fig->add_subplot(1, 3, 1);
    first->line(x, y).set_title("2D: click for data coordinates");

    // Step 39: bars and fills answer by area, a point within the radius first.
    auto areas = fig->add_subplot(1, 3, 2);
    {
        const std::vector<double> xs{0, 1, 2, 3, 4, 5, 6};
        const std::vector<std::vector<double>> layers{{1, 2, 1.5, 2.5, 2, 1, 1.5}, {1, 1, 2, 1, 1.5, 2, 1}};
        areas->stack(xs, layers, {.names = {"low", "high"}, .alpha = 0.6f});
        areas->bar(std::vector<double>{8, 9, 10}, std::vector<double>{2, 4, 3}, {.name = "bars"});
        areas->scatter(std::vector<double>{2, 9}, std::vector<double>{3, 4}, {.color = sextant::Color::Black});
        areas->legend().set_title("Areas: inside a band or bar; the dots first");
    }
    auto ax3 = fig->add_subplot3d(1, 3, 3);
    ax3->set_title("3D: data only where a plane is hit");
    ax3->plane(sextant::PlaneOrientation::XY, 0.0)->line(x, y);

    // Click an object, then press x: it is removed by its ObjectId. A second x
    // finds nothing and says so through the message handler.
    std::shared_ptr<sextant::Axes> picked_axes;
    sextant::ObjectId picked_id = 0;
    int moves = 0;
    for (int k = 0; k <= static_cast<int>(sextant::EventKind::Pick); ++k) {
        const auto kind = static_cast<sextant::EventKind>(k);
        fig->connect(kind, [&, kind](const sextant::Event& e) {
            if (kind == sextant::EventKind::MouseMove && ++moves % 20 != 0) return; // thin the log
            if (kind == sextant::EventKind::Pick) {
                picked_axes = e.axes == 1 ? first : e.axes == 2 ? areas : nullptr;
                picked_id = e.pick_object;
            }
            if (kind == sextant::EventKind::KeyDown && e.key == "x" && picked_axes) {
                const bool ok = picked_axes->remove(picked_id);
                printf("[remove] id=%llu -> %s\n", static_cast<unsigned long long>(picked_id), ok ? "removed" : "not found");
                fig->refresh();
            }
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
                    printf(" %s object=%llu index=%d row=%d col=%d plane=%llu axes=%d button=%d",
                           pick_names[static_cast<int>(e.pick_kind)], static_cast<unsigned long long>(e.pick_object), e.pick_index,
                           e.pick_row, e.pick_col, static_cast<unsigned long long>(e.pick_plane), e.axes, e.button);
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


// Line styles, widths, transparency, markers drawn over a line, error bars and
// a plane. (The step drawstyles are `part_line_steps`.)
static void part_line_styles() {
    using sextant::LineStyle;
    auto fig = sextant::Figure::create({.width = 1300, .height = 900, .title = "Line styles"});
    fig->suptitle("LineOptions: linestyle, linewidth, alpha, loop, error bars");

    std::vector<double> x(120), y(120);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.1 * static_cast<double>(i);

    // Every linestyle at two widths.
    auto a1 = fig->add_subplot(2, 2, 1);
    {
        const std::pair<LineStyle, const char*> styles[] = {
            {LineStyle::Solid, "Solid"}, {LineStyle::Dashed, "Dashed"},
            {LineStyle::Dotted, "Dotted"}, {LineStyle::DashDot, "DashDot"}};
        for (int k = 0; k < 4; ++k) {
            for (std::size_t i = 0; i < x.size(); ++i) y[i] = 2.0 * k + std::sin(x[i]);
            a1->line(x, y, {.linewidth = k % 2 ? 4.0f : 1.5f, .linestyle = styles[k].first,
                            .name = styles[k].second});
        }
        a1->legend().grid().set_title("linestyle x linewidth (1.5 / 4)");
    }

    // Transparency: overlapping wide strokes blend, one stroke does not self-overlap.
    auto a2 = fig->add_subplot(2, 2, 2);
    {
        std::vector<double> y2(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            y[i] = std::sin(x[i]);
            y2[i] = std::cos(x[i]);
        }
        a2->line(x, y, {.color = sextant::Color::Blue, .linewidth = 14.0f, .name = "alpha 0.4", .alpha = 0.4f});
        a2->line(x, y2, {.color = sextant::Color::Red, .linewidth = 14.0f, .name = "alpha 0.4", .alpha = 0.4f});
        a2->line(x, y, {.color = sextant::Color::Black, .linewidth = 1.0f, .name = "opaque"});
        a2->legend().set_title("alpha: crossings blend, no seams inside one line");
    }

    // Error bars on a line: symmetric, asymmetric and boxed, in x and y.
    auto a3 = fig->add_subplot(2, 2, 3);
    {
        std::vector<double> xs, ys, lo, hi, bx;
        for (int i = 0; i < 10; ++i) {
            xs.push_back(i);
            ys.push_back(3.0 + std::sin(0.7 * i));
            lo.push_back(0.2 + 0.03 * i);
            hi.push_back(0.5 + 0.06 * i);
            bx.push_back(0.3);
        }
        a3->line(xs, ys, {.x_cap_lo = bx, .y_cap_lo = lo, .y_cap_hi = hi},
                 {.linewidth = 2.0f, .name = "x sym, y asym",
                  .errorbar = {.capsize = 8.0f}});
        std::vector<double> ys2(ys);
        for (double& v: ys2) v -= 3.0;
        a3->line(xs, ys2, {.y_cap_lo = lo, .y_cap_hi = lo, .y_box_lo = bx},
                 {.linewidth = 2.0f, .name = "arrow caps + box",
                  .errorbar = {.capsize = 9.0f, .capstyle = sextant::CapStyle::Arrow}});
        a3->legend().grid().set_title("error bars: flat and arrow caps, a box");
    }

    // A plane carries the same line options (it strokes solid).
    auto a4 = fig->add_subplot3d(2, 2, 4);
    {
        auto pl = a4->plane(sextant::PlaneOrientation::XY, 0.0);
        for (std::size_t i = 0; i < x.size(); ++i) y[i] = std::sin(x[i]);
        pl->line(x, y, {.color = sextant::Color::Red, .linewidth = 4.0f, .name = "alpha 0.6", .alpha = 0.6f});
        std::vector<double> y3(y);
        for (double& v: y3) v *= -0.5;
        pl->line(x, y3, {.color = sextant::Color::Blue, .linewidth = 2.0f, .name = "looped", .loop = true});
        a4->legend().set_title("plane: width, alpha, loop");
    }

    savefig_and_check_memory(fig, "test_line_styles");
    fig->show(true);
}

// Heatmaps: colormaps (sequential, diverging, cyclic), origin and extents,
// contours, a NaN hole, and translucent planes crossing in 3D.
static void test_heatmap() {
    constexpr int R = 40, C = 60;
    std::vector<double> field(R * C), saddle(R * C), phase(R * C), holed;
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) {
            const double x = -3.0 + 6.0 * c / (C - 1), y = -2.0 + 4.0 * r / (R - 1);
            field[r * C + c] = std::exp(-(x * x + y * y) / 2.0);
            saddle[r * C + c] = x * y;
            phase[r * C + c] = std::atan2(y, x);
        }
    holed = field;
    for (int r = 15; r < 25; ++r)
        for (int c = 25; c < 35; ++c) holed[r * C + c] = std::nan("");

    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Heatmaps"});
    fig->suptitle("heatmap / imshow: colormap, origin, extent, contours, NaN; planes");

    fig->add_subplot(2, 3, 1)
            ->heatmap(field, R, C, {-3.0, 3.0}, {-2.0, 2.0},
                      {.cmap = sextant::Colormap::Inferno, .colorbar = true, .name = "density"})
            .set_title("Inferno on a real extent (hover: data coordinates)");

    fig->add_subplot(2)
            ->heatmap(saddle, R, C, {-3.0, 3.0}, {-2.0, 2.0},
                      {.cmap = sextant::Colormap::Coolwarm, .vmin = -6.0f, .vmax = 6.0f, .colorbar = true,
                       .name = "x * y"})
            .set_title("diverging, symmetric limits");

    fig->add_subplot(3)
            ->heatmap(phase, R, C, {-3.0, 3.0}, {-2.0, 2.0},
                      {.cmap = sextant::Colormap::Phase, .vmin = -3.14159f, .vmax = 3.14159f, .colorbar = true,
                       .name = "angle"})
            .set_title("a cyclic map: no seam at +-pi");

    fig->add_subplot(4)
            ->imshow(field, R, C, {.cmap = sextant::Colormap::Gray, .origin = "upper"})
            .set_title("origin = upper (an image)");

    fig->add_subplot(5)
            ->heatmap(field, R, C, {-3.0, 3.0}, {-2.0, 2.0},
                      {.cmap = sextant::Colormap::Viridis, .colorbar = true,
                       .contours = {0.2, 0.5, 0.8}, .contour_color = {1.0f, 1.0f, 1.0f, 0.9f},
                       .contour_linewidth = 1.5f, .contour_labels = true})
            .set_title("labelled contours; NaN-free");

    // Two translucent slices crossing in a 3D scene, different map ends.
    {
        auto ax = fig->add_subplot3d(2, 3, 6);
        ax->set_title("planes: XY x YZ, translucent")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z").set_view(-50.0, 25.0);
        ax->plane(sextant::PlaneOrientation::XY, 0.0, {.alpha = 0.6f})
                ->heatmap(holed, R, C, {-3.0, 3.0}, {-2.0, 2.0},
                          {.cmap = sextant::Colormap::Viridis, .vmin = 0.0f, .vmax = 1.0f});
        ax->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
                ->heatmap(saddle, R, C, {-3.0, 3.0}, {-2.0, 2.0},
                          {.cmap = sextant::Colormap::Coolwarm, .vmin = -6.0f, .vmax = 6.0f,
                           .colorbar = true, .name = "x * y"});
    }

    savefig_and_check_memory(fig, "test_heatmap");
    fig->show(true);
}

// Text and annotations in 2D and 3D: anchors, rotation, boxes, coordinates in
// data and in frame fractions, arrows; reference lines and spans.
static void part_text() {
    auto fig = sextant::Figure::create({.width = 1500, .height = 800, .title = "Text and annotations"});
    fig->suptitle("Axes::text / annotate, Axes3D::text / text2d / annotate");

    std::vector<double> x(100), y(100);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.1 * static_cast<double>(i);
        y[i] = std::sin(x[i]);
    }
    using sextant::Pos;
    using sextant::HAlign;
    using sextant::VAlign;

    // Anchors: the marker is the position; each text hangs off it differently.
    auto a1 = fig->add_subplot(1, 3, 1);
    {
        const std::pair<HAlign, const char*> has[] = {{HAlign::Left, "L"}, {HAlign::Center, "C"}, {HAlign::Right, "R"}};
        const std::pair<VAlign, const char*> vas[] = {{VAlign::Top, "Top"}, {VAlign::Center, "Center"},
                                                      {VAlign::Baseline, "Base"}, {VAlign::Bottom, "Bottom"}};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 4; ++j) {
                const double px = 2.0 + 3.0 * i, py = 1.0 + 1.6 * j;
                a1->scatter(std::vector<double>{px}, std::vector<double>{py},
                            {.color = sextant::Color::Red, .size = 5.0f, .show_legend = false});
                a1->text(std::string(has[i].second) + "/" + vas[j].second, px, py,
                         {.fontsize = 11.0f, .ha = has[i].first, .va = vas[j].first});
            }
        a1->set_xlim(0, 10).set_ylim(0, 6).set_title("ha x va around the red dot");
    }

    // Looks: rotation, box, alpha, multi-line, math, a frame-fraction position,
    // and an arrow in each style.
    auto a2 = fig->add_subplot(1, 3, 2);
    {
        a2->line(x, y);
        a2->text("rotated 30", 1.0, -0.8, {.rotation = 30.0f});
        a2->text("boxed", Pos::fraction(0.05), Pos::fraction(0.92),
                 {.va = VAlign::Top, .background = {1.0f, 1.0f, 0.8f, 1.0f}, .edge_linewidth = 1.0f});
        a2->text("two\nlines", Pos::fraction(0.95), Pos::fraction(0.08),
                 {.alpha = 0.5f, .ha = HAlign::Right});
        a2->text("$\\int_0^\\pi \\sin x\\,dx = 2$", Pos::fraction(0.4), Pos::fraction(0.5),
                 {.fontsize = 15.0f, .ha = HAlign::Center});
        a2->annotate(1.57, 1.0, "peak: filled", 3.2, 1.0, {.va = VAlign::Center},
                     {.head = sextant::ArrowHead::Filled});
        a2->annotate(4.71, -1.0, "trough: open, bowed", 6.0, -0.4, {.va = VAlign::Center},
                     {.head = sextant::ArrowHead::Open, .tail = sextant::ArrowHead::Bar, .linestyle = sextant::LineStyle::Dashed, .arc = 0.3f});
        a2->annotate(3.14, 0.0, "clipped (pan away)", 7.5, 0.7, {.clip_to_frame = true});
        a2->set_title("rotation, box, alpha, math, arrows");
    }

    // 3D: a point-anchored text, a fixed frame text and an annotation.
    auto a3 = fig->add_subplot3d(1, 3, 3);
    {
        std::vector<double> t(80), cx(80), cy(80), cz(80);
        for (int i = 0; i < 80; ++i) {
            t[i] = i * 0.12;
            cx[i] = std::cos(t[i]);
            cy[i] = std::sin(t[i]);
            cz[i] = 0.1 * t[i];
        }
        a3->line3d(cx, cy, cz, {.linewidth = 2.0f}).set_view(-55.0, 22.0)
                .set_xtitle("x").set_ytitle("y").set_ztitle("z").set_title("text keeps its pixel size when the box turns");
        a3->text("start", cx[0], cy[0], cz[0], {.va = VAlign::Bottom});
        a3->text("end", cx[79], cy[79], cz[79], {.color = sextant::Color::Red});
        a3->text2d("text2d: stays put", 0.03, 0.95, {.va = VAlign::Top,
                                                    .background = {1.0f, 1.0f, 0.8f, 1.0f}, .edge_linewidth = 1.0f});
        a3->annotate(cx[40], cy[40], cz[40], "$\\theta = 4.8$", 50.0, 40.0, {}, {.head = sextant::ArrowHead::Open});
    }

    savefig_and_check_memory(fig, "test_text");
    fig->show(true);
}

// Legend: all twelve anchors; colorbar: all four, width / margin / offset,
// stacking, and a 3D cell.
static void part_legend() {
    using sextant::LegendAnchor;
    auto fig = sextant::Figure::create({.width = 1700, .height = 1000, .title = "Legend anchors"});
    fig->suptitle("LegendAnchor: Inside*, Outside*; margin and offset");

    std::vector<double> x(80), a(80), b(80);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.1 * static_cast<double>(i);
        a[i] = std::sin(x[i]);
        b[i] = 0.5 * std::cos(x[i]);
    }
    const std::pair<LegendAnchor, const char*> anchors[] = {
        {LegendAnchor::InsideTL, "InsideTL"}, {LegendAnchor::InsideTR, "InsideTR"},
        {LegendAnchor::InsideBL, "InsideBL"}, {LegendAnchor::InsideBR, "InsideBR"},
        {LegendAnchor::OutsideTL, "OutsideTL"}, {LegendAnchor::OutsideTR, "OutsideTR"},
        {LegendAnchor::OutsideBL, "OutsideBL"}, {LegendAnchor::OutsideBR, "OutsideBR"},
        {LegendAnchor::OutsideLT, "OutsideLT"}, {LegendAnchor::OutsideLB, "OutsideLB"},
        {LegendAnchor::OutsideRT, "OutsideRT"}, {LegendAnchor::OutsideRB, "OutsideRB"},
    };
    for (int k = 0; k < 12; ++k) {
        auto ax = fig->add_subplot(3, 4, k + 1);
        ax->line(x, a, {.name = "sin"}).line(x, b, {.linestyle = sextant::LineStyle::Dashed, .name = "half cos"})
                .scatter(x, b, {.size = 5.0f, .name = "samples"});
        sextant::LegendOptions lo{.anchor = anchors[k].first};
        if (k == 3) lo.offset_x = -30.0f, lo.offset_y = 20.0f;
        if (k == 7) lo.margin = 30.0f;
        if (k == 11) lo.frameon = false;
        ax->legend(lo).set_title(anchors[k].second);
    }

    savefig_and_check_memory(fig, "test_legend");
    fig->show(true);
}

static void part_colorbar() {
    using sextant::ColorbarAnchor;
    auto fig = sextant::Figure::create({.width = 1500, .height = 900, .title = "Colorbar anchors"});
    fig->suptitle("ColorbarAnchor: Left, Right, Top, Bottom; width, margin, offset; stacking");

    constexpr int R = 20, C = 20;
    std::vector<double> f(R * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c) f[r * C + c] = std::sin(0.3 * r) * std::cos(0.25 * c);
    const std::pair<ColorbarAnchor, const char*> anchors[] = {
        {ColorbarAnchor::Left, "Left"}, {ColorbarAnchor::Right, "Right"},
        {ColorbarAnchor::Top, "Top"}, {ColorbarAnchor::Bottom, "Bottom"}};
    for (int k = 0; k < 4; ++k) {
        auto ax = fig->add_subplot(2, 3, k + 1);
        ax->imshow(f, R, C, {.vmin = -1.0f, .vmax = 1.0f, .colorbar = true, .name = "value"});
        sextant::ColorbarOptions co{.anchor = anchors[k].first};
        if (k == 1) co.width = 30.0f, co.margin = 25.0f;
        if (k == 3) co.offset_y = 12.0f;
        ax->set_colorbar_style(co).set_title(anchors[k].second);
    }

    // Two bars and a legend on one side: bars stack outboard of the legend.
    auto a5 = fig->add_subplot(2, 3, 5);
    {
        std::vector<double> x(40), y(40), z(40);
        for (int i = 0; i < 40; ++i) {
            x[i] = 10.0 * (i % 20) / 19.0;
            y[i] = 10.0 * (i / 20) + std::sin(x[i]);
            z[i] = i;
        }
        a5->imshow(f, R, C, {.cmap = sextant::Colormap::Gray, .vmin = -1.0f, .vmax = 1.0f, .colorbar = true,
                             .name = "field"})
                .scatter_z(x, y, z, {.cmap = sextant::Colormap::Plasma, .size = 14.0f, .colorbar = true,
                                     .name = "index"})
                .legend({.anchor = sextant::LegendAnchor::OutsideRT})
                .set_title("two bars + legend on the right");
    }

    auto a6 = fig->add_subplot3d(2, 3, 6);
    {
        std::vector<double> u(20), v(20), h(400);
        for (int i = 0; i < 20; ++i) u[i] = v[i] = -2.0 + 4.0 * i / 19.0;
        for (int j = 0; j < 20; ++j)
            for (int i = 0; i < 20; ++i) h[j * 20 + i] = std::exp(-(u[i] * u[i] + v[j] * v[j]));
        a6->surface(sextant::PlaneOrientation::XY, u, v, h,
                    {.colormap = true, .colorbar = true, .name = "height"})
                .set_colorbar_style({.anchor = ColorbarAnchor::Bottom})
                .set_title("3D, bar below");
    }

    savefig_and_check_memory(fig, "test_colorbar");
    fig->show(true);
}

// bar3d: grid footprint (width/depth), bases and negative bars, edges, hints,
// translucent interleaved grids, and a plane cutting through bars.
static void test_bar3d() {
    constexpr int NU = 13, NV = 13;
    std::vector<double> gx(NU), gy(NV), ripple(NU * NV), bowl(NU * NV);
    for (int i = 0; i < NU; ++i) gx[i] = -3.0 + 6.0 * i / (NU - 1);
    for (int j = 0; j < NV; ++j) gy[j] = -3.0 + 6.0 * j / (NV - 1);
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j) {
            const double r = std::hypot(gx[i], gy[j]);
            ripple[i * NV + j] = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
            bowl[i * NV + j] = 0.22 * (gx[i] * gx[i] - gy[j] * gy[j]);
        }
    std::vector<double> gx2(gx), gy2(gy);
    for (double& v: gx2) v += 0.25;
    for (double& v: gy2) v += 0.25;
    constexpr int PR = 40, PC = 40;
    std::vector<double> sl(PR * PC);
    for (int r = 0; r < PR; ++r)
        for (int c = 0; c < PC; ++c)
            sl[r * PC + c] = std::sin(-3.0 + 6.0 * c / (PC - 1) + 0.6) * std::cos(-3.0 + 6.0 * r / (PR - 1));
    const sextant::Range ext{-3.0, 3.0};

    auto fig = sextant::Figure::create({.width = 1500, .height = 950, .title = "bar3d", .subplot_col_gap = 0.0f});
    fig->suptitle("bar3d: footprint, bases, edges, hints, translucency");

    auto c1 = fig->add_subplot3d(2, 3, 1);
    c1->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {.color = sextant::Color::Blue, .width = 0.8f, .depth = 0.8f, .bottom = -1.5,
               .edges = true, .edgecolor = sextant::Color::Black, .name = "ripple"})
            .set_title("bottom -1.5, edges, negative bars hang down")
            .set_xtitle("x").set_ytitle("y").set_ztitle("z").set_view(-55.0, 25.0).legend();

    auto c2 = fig->add_subplot3d(2, 3, 2);
    c2->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {.color = sextant::Color::Orange, .alpha = 0.5f, .width = 0.45f, .depth = 0.45f, .bottom = -1.5});
    c2->bar3d(sextant::PlaneOrientation::XY, gx2, gy2, bowl,
              {.color = sextant::Color::Blue, .alpha = 0.5f, .width = 0.45f, .depth = 0.45f, .bottom = -1.5});
    c2->set_title("bar x bar (interleaved, alpha 0.5)").set_xtitle("x").set_ytitle("y").set_ztitle("z");

    auto c3 = fig->add_subplot3d(2, 3, 3);
    {
        std::vector<std::string> hints(NU * NV);
        hints[(NU / 2) * NV + NV / 2] = "centre bar";
        hints[0] = "corner bar";
        c3->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
                  {.color = sextant::Color::Green, .width = 0.8f, .depth = 0.8f, .bottom = -1.5,
                   .hint_labels = hints});
        c3->set_title("hover: x, y, height, labels").set_xtitle("x").set_ytitle("y").set_ztitle("z");
    }

    auto c4 = fig->add_subplot3d(2, 3, 4);
    c4->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {.color = sextant::Color::Green, .alpha = 0.5f, .width = 0.8f, .depth = 0.8f, .bottom = -1.5});
    c4->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
            ->heatmap(sl, PR, PC, ext, ext, {.vmin = -1.0f, .vmax = 1.0f});
    c4->set_title("plane x bar (a cut through the grid)").set_xtitle("x").set_ytitle("y").set_ztitle("z");

    std::vector<double> midway(NU * NV);
    for (std::size_t k = 0; k < midway.size(); ++k) midway[k] = -1.5 + 0.5 * ripple[k];
    auto c5 = fig->add_subplot3d(2, 3, 5);
    c5->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
              {.color = sextant::Color::Orange, .alpha = 0.5f, .width = 0.7f, .depth = 0.7f, .bottom = -1.5});
    c5->surface(sextant::PlaneOrientation::XY, gx, gy, midway,
                {.colormap = true, .alpha = 0.55f});
    c5->set_title("bar x surface (sheet inside the bars)").set_xtitle("x").set_ytitle("y").set_ztitle("z");

    // The other two orientations of a grid.
    auto c6 = fig->add_subplot3d(2, 3, 6);
    c6->bar3d(sextant::PlaneOrientation::YZ, gx, gy, bowl, {.color = sextant::Color::Purple, .alpha = 0.8f,
                                                            .width = 0.6f, .depth = 0.6f, .name = "YZ"});
    c6->set_title("orientation YZ: bars grow along x").set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .legend().set_view(-35.0, 20.0);

    savefig_and_check_memory(fig, "test_bar3d");
    fig->show(true);
}

// surface: colormapped / flat, shading, wireframe, orientation, translucent
// crossings, perspective.
static void test_surface() {
    constexpr int N = 25;
    std::vector<double> u(N), v(N), ripple(N * N), dome(N * N), saddle(N * N);
    for (int i = 0; i < N; ++i) u[i] = v[i] = -3.0 + 6.0 * i / (N - 1);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            const double r = std::hypot(u[i], v[j]);
            ripple[i * N + j] = 2.0 * std::exp(-r * 0.5) * std::cos(r * 2.2);
            dome[i * N + j] = 3.0 - 0.3 * r * r;
            saddle[i * N + j] = 0.22 * (u[i] * u[i] - v[j] * v[j]);
        }
    constexpr int PR = 40, PC = 40;
    std::vector<double> sl(PR * PC);
    for (int r = 0; r < PR; ++r)
        for (int c = 0; c < PC; ++c)
            sl[r * PC + c] = std::sin(-3.0 + 6.0 * c / (PC - 1)) * std::cos(-3.0 + 6.0 * r / (PR - 1));
    const sextant::Range ext{-3.0, 3.0};

    auto fig = sextant::Figure::create({.width = 1500, .height = 950, .title = "surface"});
    fig->suptitle("surface: colormap, shading, wireframe, orientation, translucency, perspective");

    fig->add_subplot3d(2, 3, 1)
            ->surface(sextant::PlaneOrientation::XY, u, v, ripple,
                      {.colormap = true, .cmap = sextant::Colormap::Coolwarm, .colorbar = true,
                       .name = "amplitude", .shading = 0.35f})
            .set_title("colormapped, diverging").set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .set_view(-50.0, 28.0);

    fig->add_subplot3d(2, 3, 2)
            ->surface(sextant::PlaneOrientation::XY, u, v, dome,
                      {.color = sextant::Color::Cyan, .shading = 0.5f, .edges = true, .edge_linewidth = 0.8f})
            .set_title("flat colour + wireframe").set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .set_view(-40.0, 22.0);

    fig->add_subplot3d(2, 3, 3)
            ->surface(sextant::PlaneOrientation::YZ, u, v, ripple,
                      {.colormap = true, .cmap = sextant::Colormap::Viridis, .shading = 0.0f})
            .set_title("orientation YZ, shading 0 (flat lighting)").set_xtitle("x").set_ytitle("y").set_ztitle("z")
            .set_view(-30.0, 20.0);

    auto c4 = fig->add_subplot3d(2, 3, 4);
    c4->surface(sextant::PlaneOrientation::XY, u, v, ripple,
                {.color = sextant::Color::from_hex(0xff5533), .alpha = 0.55f});
    c4->surface(sextant::PlaneOrientation::XY, u, v, saddle,
                {.color = sextant::Color::from_hex(0x3388ff), .alpha = 0.55f});
    c4->set_title("surface x surface (two crossings, alpha 0.55)").set_xtitle("x").set_ytitle("y").set_ztitle("z");

    auto c5 = fig->add_subplot3d(2, 3, 5);
    c5->surface(sextant::PlaneOrientation::XY, u, v, ripple,
                {.colormap = true, .cmap = sextant::Colormap::Viridis, .alpha = 0.6f});
    c5->plane(sextant::PlaneOrientation::XY, 0.35, {.alpha = 0.6f})
            ->heatmap(sl, PR, PC, ext, ext, {.vmin = -1.0f, .vmax = 1.0f});
    c5->set_title("plane x surface").set_xtitle("x").set_ytitle("y").set_ztitle("z");

    fig->add_subplot3d(2, 3, 6)
            ->surface(sextant::PlaneOrientation::XY, u, v, ripple,
                      {.colormap = true, .colorbar = true, .name = "z", .alpha = 0.85f, .shading = 0.35f})
            .set_projection(sextant::Projection::Perspective).set_fov(38.0).set_view(-48.0, 24.0)
            .set_title("perspective, alpha 0.85").set_xtitle("x").set_ytitle("y").set_ztitle("z");

    savefig_and_check_memory(fig, "test_surface");
    fig->show(true);
}

// scatter3d: every marker shape, per-point sizes, depth shading, a colormapped
// cloud. (Error bars are `part_errorbar3d`.)
static void part_scatter3d_markers() {
    using sextant::MarkerStyle;
    auto fig = sextant::Figure::create({.width = 1400, .height = 800, .title = "scatter3d markers"});
    fig->suptitle("scatter3d: marker shapes, sizes, depthshade, colormap");

    const MarkerStyle all[] = {
        MarkerStyle::Circle, MarkerStyle::Square, MarkerStyle::Triangle, MarkerStyle::Diamond,
        MarkerStyle::Star, MarkerStyle::Hexagon, MarkerStyle::Cross, MarkerStyle::Plus};
    auto a1 = fig->add_subplot3d(1, 3, 1);
    for (int k = 0; k < 8; ++k) {
        const std::vector<double> x{static_cast<double>(k)}, y{0.0}, z{0.0};
        a1->scatter3d(x, y, z, {.size = 24.0f, .marker = all[k], .edgecolor = sextant::Color::Black,
                                .edge_linewidth = 1.0f});
    }
    a1->set_title("shapes along x").set_xtitle("x").set_ytitle("y").set_ztitle("z").set_view(-30.0, 20.0);

    auto a2 = fig->add_subplot3d(1, 3, 2);
    {
        std::vector<double> x, y, z;
        sextant::Scatter3DOptions o;
        o.name = "sized";
        o.alpha = 0.8f;
        for (int i = 0; i < 60; ++i) {
            const double t = 0.35 * i;
            x.push_back(std::cos(t) * (1.0 + 0.05 * i));
            y.push_back(std::sin(t) * (1.0 + 0.05 * i));
            z.push_back(0.1 * i);
            o.sizes.push_back(5.0 + 0.6 * i);
        }
        a2->scatter3d(x, y, z, o).legend().set_title("sizes 5..40, alpha 0.8")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z").set_view(-50.0, 25.0);
    }

    auto a3 = fig->add_subplot3d(1, 3, 3);
    {
        std::vector<double> x, y, z, c;
        for (int i = 0; i < 400; ++i) {
            const double t = i * 0.37;
            x.push_back(2.5 * std::sin(t * 0.9));
            y.push_back(2.5 * std::cos(t * 0.6));
            z.push_back(-2.0 + 4.0 * (i % 40) / 39.0);
            c.push_back(z.back());
        }
        a3->scatter3d(x, y, z, c, {.size = 14.0f, .depthshade = 0.8f, .colorbar = true, .name = "height"})
                .set_projection(sextant::Projection::Perspective).set_fov(40.0)
                .set_title("colormapped, depthshade 0.8, perspective")
                .set_xtitle("x").set_ytitle("y").set_ztitle("z").set_view(-55.0, 20.0);
    }

    savefig_and_check_memory(fig, "test_scatter3d_markers");
    fig->show(true);
}

// Animation and live update: every data kind replaced from the main thread
// while the window runs, through set_*_data() and refresh(). Pan, zoom and the
// titles survive; the legend and the colorbar follow.
static void test_animation() {
    constexpr int N = 120, R = 30, C = 30, G = 21;
    std::vector<double> x(N), y(N), px(40), py(40), pz(40), hm(R * C), bx(6), bh(6);
    std::vector<double> gu(G), gv(G), gh(G * G);
    for (int i = 0; i < N; ++i) x[i] = i * 2.0 * M_PI / N;
    for (int i = 0; i < 6; ++i) bx[i] = i + 1;
    for (int i = 0; i < G; ++i) gu[i] = gv[i] = -3.0 + 6.0 * i / (G - 1);

    auto fig = sextant::Figure::create({.width = 1400, .height = 900, .title = "Animation"});
    fig->suptitle("set_line_data / scatter_z / heatmap / bar / surface, refresh() at 25 fps");
    for (int i = 0; i < N; ++i) y[i] = std::sin(x[i]);
    for (int i = 0; i < 40; ++i) px[i] = py[i] = pz[i] = 0.0;
    auto a1 = fig->add_subplot(2, 3, 1);
    a1->line(x, y, {.linewidth = 2.0f, .name = "sin(x - t)"}).set_ylim(-1.5, 1.5).legend().grid()
            .set_title("line");
    auto a2 = fig->add_subplot(2, 3, 2);
    a2->scatter_z(px, py, pz, {.colorbar = true, .name = "t"}).set_xlim(-2, 2).set_ylim(-2, 2)
            .set_aspect(1.0).set_title("scatter_z: orbiting");
    auto a3 = fig->add_subplot(2, 3, 3);
    a3->imshow(hm, R, C, {.vmin = -1.0f, .vmax = 1.0f, .colorbar = true}).set_title("heatmap");
    auto a4 = fig->add_subplot(2, 3, 4);
    a4->bar(bx, bh).set_ylim(0, 6).set_title("bar");
    auto a5 = fig->add_subplot3d(2, 3, 5);
    a5->surface(sextant::PlaneOrientation::XY, gu, gv, gh, {.colormap = true})
            .set_zlim(-1.5, 1.5).set_view(-50.0, 25.0).set_title("surface");
    auto a6 = fig->add_subplot(2, 3, 6);
    a6->axline(0.0, {.name = "moving level"}).text("label", 1.0, 0.0).set_xlim(0, 6).set_ylim(-1, 1)
            .set_title("axline + text");

    fig->show(false);
    for (int f = 0; f < 150; ++f) {
        pump_for(std::chrono::milliseconds(40));
        const double t = f * 0.08;
        for (int i = 0; i < N; ++i) y[i] = std::sin(x[i] - t);
        a1->set_line_data(0, {x, y});
        for (int i = 0; i < 40; ++i) {
            const double a = t + i * 0.157;
            px[i] = 1.6 * std::cos(a);
            py[i] = 1.6 * std::sin(a);
            pz[i] = i + f;
        }
        a2->set_scatter_z_data(0, px, py, pz);
        for (int r = 0; r < R; ++r)
            for (int c = 0; c < C; ++c) hm[r * C + c] = std::sin(0.4 * c - t) * std::cos(0.3 * r + 0.5 * t);
        a3->set_heatmap_data(0, hm, R, C, {0.0, 30.0}, {0.0, 30.0});
        for (int i = 0; i < 6; ++i) bh[i] = 3.0 + 2.0 * std::sin(t + i);
        a4->set_bar_data(0, bx, bh);
        for (int i = 0; i < G; ++i)
            for (int j = 0; j < G; ++j) gh[i * G + j] = std::cos(std::hypot(gu[i], gv[j]) * 2.0 - t);
        a5->set_surface_data(0, sextant::PlaneOrientation::XY, gu, gv, gh);
        a6->set_axline_data(0, {.value = std::sin(t)});
        a6->set_text_data(0, "label", sextant::Pos(1.0 + 4.0 * (0.5 + 0.5 * std::sin(0.5 * t))), sextant::Pos(std::sin(t)));
        fig->refresh();
        if (f == 75) savefig_and_check_memory(fig, "test_animation");
    }
    fig->close();
    printf("[main] animation done\n");
}

// ============================ the tests ============================
// One topic each; a test that has several figures shows them one after the
// other (close a window to advance).

static void test_figure_layout() { // spans, shared axes, twins
    part_spans();
    part_shared();
    part_twins();
}

static void test_aspect_scales() {
    part_aspect();
    part_scales();
}

static void test_decorations() { // text, annotate (2D and 3D), axline, axspan
    part_text();
    part_reference_lines();
}

static void test_legend_colorbar() {
    part_legend();
    part_colorbar();
}

static void test_line() {
    part_line_styles();
    part_line_steps();
}

static void test_scatter() { // scatter, scatter_z
    part_marker_shapes();
    part_marker_sizes();
}

static void test_bar() { // bar, hist
    part_bar_orientation();
}

static void test_fill() { // fill, stack
    part_fill_between();
    part_stack();
}

static void test_scatter3d() {
    part_scatter3d_markers();
    part_errorbar3d();
}

static void test_show_nonblocking() {
    part_refresh_before_show();
    part_nonblocking();
    part_wait_and_run();
}

int main() {
    test_axes_gallery();
    // test_axes3d_gallery();
    // test_figure_layout();
    // test_aspect_scales();
    // test_mathtext_gallery();
    // test_decorations();
    // test_legend_colorbar();
    // test_line();
    // test_scatter();
    // test_bar();
    // test_heatmap();
    // test_fill();
    // test_quiver();
    // test_bar3d();
    // test_surface();
    // test_surface_tri();
    // test_line3d();
    // test_scatter3d();
    // test_show_nonblocking();
    // test_animation();
    // test_events();
}
