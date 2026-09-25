// sextant -- performance test harness: RAM for large datasets, live-window
// smoothness, and the cost of supersampling and stroke expansion.
//
// Usage:  sextant_perf_test [all|ingest|snapshot|render|export|peel|svg3d|edit|interactive]
//         No argument prompts (falling back to "all" when stdin is closed).
//         "all" runs only the automated sections.
//
// Method:
//  - No savefig() in the render sections (each builds a ~200 ms headless
//    context); render cost comes from a live window's frame_stats().
//  - frame_stats() excludes the vsync swap, so ms/frame isn't clamped; the
//    reported FPS is.
//  - Warm-up frames (shader compile, atlas upload, etc.) are discarded.
//  - Data uses a fixed seed.

#include <sextant/sextant.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
// NOMINMAX, or <windows.h> breaks std::min/max. rpcndr.h still #defines
// `small`, so no local uses that name.
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <psapi.h>
#elif defined(__linux__)
#  include <cstdio>
#  include <unistd.h>
#endif

namespace {
    using Clock = std::chrono::steady_clock;

    double ms_since(Clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }

    // ---------------------------------------------------------------------------
    // Terminal prompts. All fall back to their default on closed/empty stdin, so
    // piped runs never hang.
    // ---------------------------------------------------------------------------
    bool read_line(std::string& out) {
        if (!std::getline(std::cin, out)) return false;
        // Trim stray spaces and Windows CRs.
        const auto first = out.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            out.clear();
            return true;
        }
        const auto last = out.find_last_not_of(" \t\r\n");
        out = out.substr(first, last - first + 1);
        return true;
    }

    std::string prompt_choice(const std::string& question,
                              const std::vector<std::string>& options,
                              const std::string& fallback) {
        std::printf("%s\n", question.c_str());
        for (std::size_t i = 0; i < options.size(); ++i)
            std::printf("  %zu) %s\n", i + 1, options[i].c_str());

        // Typos re-prompt (long runs shouldn't start by mistake); EOF takes the
        // fallback.
        for (;;) {
            std::printf("choice [%s]: ", fallback.c_str());
            std::string line;
            if (!read_line(line)) {
                // stdin closed
                std::printf("%s\n", fallback.c_str()); // echo, so piped logs read sensibly
                return fallback;
            }
            if (line.empty()) {
                std::printf("%s\n", fallback.c_str());
                return fallback;
            }
            // Accept the index or the name.
            if (line.size() <= 2 && std::isdigit(static_cast<unsigned char>(line[0]))) {
                const int idx = std::atoi(line.c_str());
                if (idx >= 1 && idx <= static_cast<int>(options.size()))
                    return options[static_cast<std::size_t>(idx) - 1];
            }
            for (const auto& o: options)
                if (o == line) return o;

            std::printf("  '%s' is not one of the options.\n", line.c_str());
        }
    }

    sextant::LineStyle line_style_from(const std::string& name) {
        if (name == "dashed") return sextant::LineStyle::Dashed;
        if (name == "dotted") return sextant::LineStyle::Dotted;
        if (name == "dashdot") return sextant::LineStyle::DashDot;
        return sextant::LineStyle::Solid;
    }

    std::size_t prompt_size(const std::string& question, std::size_t fallback) {
        std::printf("%s [%zu]: ", question.c_str(), fallback);
        std::string line;
        if (!read_line(line) || line.empty()) {
            std::printf("%zu\n", fallback);
            return fallback;
        }
        const long long v = std::atoll(line.c_str());
        if (v <= 0) {
            std::printf("not a positive number — using %zu\n", fallback);
            return fallback;
        }
        return static_cast<std::size_t>(v);
    }

    // ---------------------------------------------------------------------------
    // Resident memory (working set / RSS): what the process costs, including the
    // library's internal copies.
    // ---------------------------------------------------------------------------
    double resident_mb() {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc{};
        // K32-prefixed: in kernel32, no extra link library.
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        return 0.0;
#elif defined(__linux__)
        long rss_pages = 0;
        if (FILE* f = std::fopen("/proc/self/statm", "r")) {
            long total = 0;
            if (std::fscanf(f, "%ld %ld", &total, &rss_pages) != 2) rss_pages = 0;
            std::fclose(f);
        }
        return static_cast<double>(rss_pages) * static_cast<double>(sysconf(_SC_PAGESIZE))
               / (1024.0 * 1024.0);
#else
        return 0.0; // unsupported platform — memory columns will read 0
#endif
    }

    // ---------------------------------------------------------------------------
    // Deterministic data
    // ---------------------------------------------------------------------------
    struct Series {
        std::vector<double> x, y, z;
    };

    Series make_series(std::size_t n, unsigned seed = 12345) {
        std::mt19937 rng(seed);
        std::normal_distribution<double> jitter(0.0, 0.05);
        Series s;
        s.x.reserve(n);
        s.y.reserve(n);
        s.z.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) * 0.001;
            s.x.push_back(t);
            // Direction changes, so draw_lines() exercises its miter path.
            s.y.push_back(std::sin(t) * std::cos(t * 0.37) + jitter(rng));
            s.z.push_back(static_cast<double>(i % 1000) / 1000.0);
        }
        return s;
    }

    std::vector<double> make_matrix(int rows, int cols) {
        std::vector<double> m(static_cast<std::size_t>(rows) * cols);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                m[static_cast<std::size_t>(r) * cols + c] =
                        std::sin(r * 0.05) * std::cos(c * 0.05);
        return m;
    }

    // ---------------------------------------------------------------------------
    // Live-window sampling
    // ---------------------------------------------------------------------------
    //  - `cpu_ms`: frame_stats()'s CPU cost of submitting a frame (no GPU time).
    //  - `frame_ms`: wall-clock per frame with vsync off, GPU included (the only
    //    one that shows supersample cost).
    // GPU-bound where frame_ms >> cpu_ms, CPU-bound where they meet.
    struct RenderSample {
        double cpu_ms = 0.0;
        double frame_ms = 0.0;
        double fps = 0.0; // 1000 / frame_ms; vsync is off, so uncapped
        double max_ms = 0.0; // worst submission since show()
        unsigned long long frames = 0;
    };

    // Opens fig, skips warm-up, then samples frame_stats() over `seconds` by
    // differencing two cumulative readings.
    RenderSample sample_render(const std::shared_ptr<sextant::Figure>& fig,
                               double seconds = 2.0,
                               unsigned long long warmup_frames = 30) {
        fig->show(false); // non-blocking: window runs on its own thread

        // Wait for warm-up frames (bounded, in case the window never renders).
        const auto warm_deadline = Clock::now() + std::chrono::seconds(10);
        while (fig->frame_stats().frames < warmup_frames && Clock::now() < warm_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const sextant::FrameStats a = fig->frame_stats();
        const auto t0 = Clock::now();
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        const sextant::FrameStats b = fig->frame_stats();
        const double wall_s = ms_since(t0) / 1000.0;

        RenderSample r;
        r.frames = b.frames - a.frames;
        if (r.frames > 0) {
            r.cpu_ms = (b.total_ms - a.total_ms) / static_cast<double>(r.frames);
            r.fps = static_cast<double>(r.frames) / wall_s;
            r.frame_ms = wall_s * 1000.0 / static_cast<double>(r.frames);
        }
        r.max_ms = b.max_ms; // cumulative worst; warm-up spikes are excluded by
        // Includes construction if before `a`: reported as
        // "worst since show()", not per interval
        fig->close();
        return r;
    }

    // Vsync off throughout (see RenderSample).
    sextant::FigureOptions perf_opts(const std::string& title, int supersample) {
        sextant::FigureOptions o;
        o.width = 1280;
        o.height = 800;
        o.title = title;
        o.supersample = supersample;
        o.vsync = false;
        return o;
    }

    const std::size_t kSizes[] = {1000, 10000, 100000, 1000000};

    // ---------------------------------------------------------------------------
    // 1. Ingest — construction cost and resident memory
    // ---------------------------------------------------------------------------
    void bench_ingest() {
        std::printf("\n=== 1. Ingest: Axes::line()/scatter() cost + resident memory ===\n");
        std::printf("Each row builds a fresh Figure. 'RAM delta' is process working set\n");
        std::printf("after minus before, so it includes the copy Axes::Impl keeps.\n\n");
        std::printf("%-10s %10s %12s %12s %12s\n",
                    "kind", "points", "build ms", "RAM delta MB", "bytes/pt");
        std::printf("%-10s %10s %12s %12s %12s\n",
                    "----------", "----------", "------------", "------------", "------------");

        for (std::size_t n: kSizes) {
            const Series s = make_series(n);
            for (int kind = 0; kind < 2; ++kind) {
                const double before = resident_mb();
                auto fig = sextant::Figure::create(perf_opts("ingest", 1));
                auto ax = fig->axes();

                const auto t0 = Clock::now();
                if (kind == 0) ax->line(s.x, s.y);
                else ax->scatter(s.x, s.y);
                const double build = ms_since(t0);

                const double after = resident_mb();
                const double delta = after - before;
                std::printf("%-10s %10zu %12.2f %12.1f %12.1f\n",
                            kind == 0 ? "line" : "scatter", n, build, delta,
                            n ? delta * 1024.0 * 1024.0 / static_cast<double>(n) : 0.0);
            }
        }
        std::printf("\nReference: the caller's own two std::vector<double> are %s.\n",
                    "16 bytes/point, and Axes::Impl keeps its own copy");
    }

    // ---------------------------------------------------------------------------
    // 2. Snapshot — refresh() cost
    // ---------------------------------------------------------------------------
    void bench_snapshot() {
        std::printf("\n=== 2. refresh(): snapshot hand-off cost on the caller thread ===\n");
        std::printf("build_figure_snapshot() deep-copies every plot vector per call, so\n");
        std::printf("this is what a live-updating program pays per update, and it is\n");
        std::printf("*not* hidden by the render thread.\n\n");
        std::printf("%-10s %10s %14s %14s\n", "kind", "points", "refresh ms", "MB resident");
        std::printf("%-10s %10s %14s %14s\n", "----------", "----------", "--------------", "--------------");

        for (std::size_t n: kSizes) {
            const Series s = make_series(n);
            auto fig = sextant::Figure::create(perf_opts("snapshot", 1));
            auto ax = fig->axes();
            ax->line(s.x, s.y);
            fig->show(false);

            // One refresh to settle, then the median of a few.
            fig->refresh();
            std::vector<double> t;
            for (int i = 0; i < 5; ++i) {
                const auto t0 = Clock::now();
                fig->refresh();
                t.push_back(ms_since(t0));
            }
            std::sort(t.begin(), t.end());
            const double resident = resident_mb();
            fig->close();

            std::printf("%-10s %10zu %14.3f %14.1f\n", "line", n, t[t.size() / 2], resident);
        }
    }

    // ---------------------------------------------------------------------------
    // 3. Render
    // ---------------------------------------------------------------------------
    void bench_render() {
        std::printf("\n=== 3. Live render cost vs data size and supersample ===\n");
        std::printf("Vsync is OFF, so these are true costs, not refresh-rate artefacts.\n");
        std::printf("  frame ms = end-to-end wall time per frame (GPU included)\n");
        std::printf("  cpu ms   = CPU submission only; GL is async, so this sees no GPU work\n");
        std::printf("GPU-bound where frame >> cpu; CPU-bound where they converge.\n");
        std::printf("A 60 Hz budget is 16.7 ms/frame.\n\n");
        std::printf("%-10s %10s %6s %10s %10s %10s\n",
                    "kind", "points", "ss", "frame ms", "cpu ms", "FPS");
        std::printf("%-10s %10s %6s %10s %10s %10s\n",
                    "----------", "----------", "------", "----------", "----------", "----------");

        const int supersamples[] = {1, 2, 4};

        for (std::size_t n: kSizes) {
            const Series s = make_series(n);
            for (int ss: supersamples) {
                {
                    // line — exercises build_polyline(): 12*(N-1) floats per frame
                    auto fig = sextant::Figure::create(perf_opts("perf line", ss));
                    fig->axes()->line(s.x, s.y);
                    const RenderSample r = sample_render(fig);
                    std::printf("%-10s %10zu %6d %10.2f %10.2f %10.1f\n",
                                "line", n, ss, r.frame_ms, r.cpu_ms, r.fps);
                } {
                    // scatter — instanced, 3 floats per point per frame
                    auto fig = sextant::Figure::create(perf_opts("perf scatter", ss));
                    sextant::ScatterOptions so;
                    so.size = 6.0f;
                    fig->axes()->scatter(s.x, s.y, so);
                    const RenderSample r = sample_render(fig);
                    std::printf("%-10s %10zu %6d %10.2f %10.2f %10.1f\n",
                                "scatter", n, ss, r.frame_ms, r.cpu_ms, r.fps);
                }
            }
        }

        // Fill-rate control: tiny data, varying window area x ss^2. The rows come
        // out flat because with vsync off these timings exclude GPU execution; the
        // `export` section prices supersampling.
        std::printf("\n--- fill rate CONTROL: trivial data, varying window x supersample ---\n");
        std::printf("Expect flat rows. That is the point: it demonstrates these live\n");
        std::printf("numbers do NOT capture GPU cost, so they cannot price supersampling.\n");
        std::printf("%-14s %6s %10s %10s %14s\n", "window", "ss", "frame ms", "cpu ms", "target Mpx");
        std::printf("%-14s %6s %10s %10s %14s\n", "--------------", "------", "----------", "----------",
                    "--------------");
        // Not named `small` (rpcndr.h #defines it).
        const Series tiny = make_series(1000);
        const int wins[][2] = {{640, 480}, {1280, 800}, {1920, 1200}, {2560, 1600}};
        for (const auto& w: wins) {
            for (int ss: {1, 2, 4}) {
                sextant::FigureOptions o = perf_opts("perf fill", ss);
                o.width = w[0];
                o.height = w[1];
                auto fig = sextant::Figure::create(o);
                fig->axes()->line(tiny.x, tiny.y);
                const RenderSample r = sample_render(fig);
                char label[32];
                std::snprintf(label, sizeof(label), "%dx%d", w[0], w[1]);
                std::printf("%-14s %6d %10.2f %10.2f %14.1f\n", label, ss,
                            r.frame_ms, r.cpu_ms,
                            static_cast<double>(w[0]) * w[1] * ss * ss / 1.0e6);
            }
        }

        // Heatmaps sweep matrix size (the texture is re-uploaded every frame).
        std::printf("\n--- heatmap (matrix re-uploaded per frame by draw_heatmap) ---\n");
        std::printf("%-14s %10s %6s %10s %10s %10s\n", "matrix", "cells", "ss", "frame ms", "cpu ms", "FPS");
        std::printf("%-14s %10s %6s %10s %10s %10s\n", "--------------", "----------", "------", "----------",
                    "----------", "----------");
        const int dims[][2] = {{64, 64}, {256, 256}, {1024, 1024}, {2048, 2048}};
        for (const auto& d: dims) {
            const std::vector<double> m = make_matrix(d[0], d[1]);
            for (int ss: {1, 2}) {
                auto fig = sextant::Figure::create(perf_opts("perf heatmap", ss));
                fig->axes()->imshow(m, d[0], d[1]);
                const RenderSample r = sample_render(fig);
                char label[32];
                std::snprintf(label, sizeof(label), "%dx%d", d[0], d[1]);
                std::printf("%-14s %10d %6d %10.2f %10.2f %10.1f\n",
                            label, d[0] * d[1], ss, r.frame_ms, r.cpu_ms, r.fps);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // 4c. Depth peeling — the exact translucent path's cost
    // ---------------------------------------------------------------------------
    // Timed through savefig() (glReadPixels synchronizes, so GPU time is included).
    // Run twice and compare:
    //
    //     sextant_perf_test peel                            (peeling, 8 layers)
    //     SEXTANT_PEEL_LAYERS=0 sextant_perf_test peel      (whole-object order)
    //
    // The opaque row is the control and must not move between runs.
    //   4c.1  bar grid vs surface at three supersamples (the baseline)
    //   4c.2  K stacked translucent slices (depth complexity, little geometry)
    //   4c.3  one surface, 25² to 200² samples (geometry, fixed depth complexity)
    //   4c.4  every kind translucent at once, two window sizes
    // Each figure is shown first so savefig() reuses the window's context.
    void bench_peel() {
        std::printf("\n=== 4c. Translucent 3D: depth peeling vs the whole-object order ===\n");
        if (const char* env = std::getenv("SEXTANT_PEEL_LAYERS"))
            std::printf("SEXTANT_PEEL_LAYERS=%s\n", env);
        else
            std::printf("SEXTANT_PEEL_LAYERS unset (peeling on, 8 layers)\n");
        std::printf("\nEvery figure is shown once first, so savefig() routes onto the window\n");
        std::printf("thread and reuses its context (see 4b) -- otherwise ~50 ms of headless\n");
        std::printf("context creation per call would swamp what is being measured. One\n");
        std::printf("untimed save warms the caches, five timed saves follow, and the median\n");
        std::printf("is reported. 'delta' is against the same scene made opaque, which is the\n");
        std::printf("only row in each block that enters neither translucent path.\n\n");
        std::printf("Run the section twice -- once unset, once with SEXTANT_PEEL_LAYERS=0 --\n");
        std::printf("and read the two tables against each other. The layer count is read once\n");
        std::printf("per process, so a sweep over it is a sweep over runs.\n");

        constexpr int kReps = 5;
        auto timed = [&](auto& fig) {
            fig->show(false);
            fig->savefig("perf_peel.png"); // warm-up: lazily-built caches
            std::vector<double> t;
            for (int i = 0; i < kReps; ++i) {
                const auto t0 = Clock::now();
                fig->savefig("perf_peel.png");
                t.push_back(ms_since(t0));
            }
            std::sort(t.begin(), t.end());
            fig->close();
            return t[kReps / 2];
        };

        auto grid = [](int n, double lo, double hi) {
            std::vector<double> g(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                g[static_cast<std::size_t>(i)] = lo + (hi - lo) * i / (n - 1);
            return g;
        };
        auto ripple_on = [](const std::vector<double>& u, const std::vector<double>& v) {
            std::vector<double> h(u.size() * v.size());
            for (std::size_t i = 0; i < u.size(); ++i)
                for (std::size_t j = 0; j < v.size(); ++j) {
                    const double r = std::hypot(u[i], v[j]);
                    h[i * v.size() + j] = 2.0 * std::exp(-r / 3.0) * std::cos(r * 1.6);
                }
            return h;
        };
        auto slice = [](int rows, int cols, double phase) {
            std::vector<double> m(static_cast<std::size_t>(rows) * cols);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c) {
                    const double x = -4.0 + 8.0 * c / (cols - 1);
                    const double y = -4.0 + 8.0 * r / (rows - 1);
                    m[static_cast<std::size_t>(r) * cols + c] =
                            std::sin(x + phase) * std::cos(y - phase);
                }
            return m;
        };
        auto fig_at = [&](int w, int h, int ss) {
            sextant::FigureOptions o = perf_opts("peel", ss);
            o.width = w;
            o.height = h;
            return sextant::Figure::create(o);
        };

        const sextant::Range kExt{-4.0, 4.0};

        // -- 4c.1  bar grid against surface, swept over supersample -------------
        // A 17x17 translucent bar grid and a translucent surface; two elevations
        // (near zero is the deepest case).
        std::printf("\n--- 4c.1  bar grid x surface, 17x17, 900x700 ---\n");
        std::printf("%-22s %6s %12s %12s %14s\n",
                    "scene", "ss", "savefig ms", "delta ms", "target Mpx");
        std::printf("%-22s %6s %12s %12s %14s\n",
                    "----------------------", "------", "------------", "------------",
                    "--------------"); {
            const std::vector<double> cu = grid(17, -4.0, 4.0), cv = grid(17, -4.0, 4.0);
            const std::vector<double> ch = ripple_on(cu, cv);
            double base = 0.0;
            auto row = [&](const char* label, double elev, float alpha, int ss) {
                auto fig = fig_at(900, 700, ss);
                auto ax = fig->add_subplot3d(1, 1, 1);
                ax->set_view(-60.0, elev);
                ax->bar3d(sextant::PlaneOrientation::XY, cu, cv, ch,
                          {
                              .color = sextant::Color::Orange, .alpha = alpha,
                              .width = 0.7f, .depth = 0.7f, .bottom = -2.5
                          });
                ax->surface(sextant::PlaneOrientation::XY, cu, cv, ch,
                            {
                                .colormap = true, .cmap = sextant::Colormap::Viridis,
                                .alpha = alpha
                            });
                const double ms = timed(fig);
                if (alpha >= 1.0f) base = ms;
                std::printf("%-22s %6d %12.1f %12.1f %14.1f\n", label, ss, ms, ms - base,
                            900.0 * 700.0 * ss * ss / 1.0e6);
            };
            for (int ss: {1, 2, 4}) {
                row("opaque (control)", 25.0, 1.00f, ss);
                row("translucent, elev 25", 25.0, 0.55f, ss);
                row("translucent, elev 2", 2.0, 0.55f, ss);
            }
        }

        // -- 4c.2  depth complexity: K stacked translucent slices ---------------
        // K layers on nearly every pixel with almost no geometry. Peeling runs
        // min(K, 8) passes; the fallback composite is quadratic per fragment in the
        // group size. The opaque control is cheaper (early-Z), so compare deltas
        // across runs, not absolutes.
        std::printf("\n--- 4c.2  depth complexity: K stacked slices, 900x700, ss 2 ---\n");
        std::printf("%-8s %10s %12s %12s %12s\n",
                    "slices", "quads", "opaque ms", "alpha ms", "delta ms");
        std::printf("%-8s %10s %12s %12s %12s\n",
                    "--------", "----------", "------------", "------------", "------------"); {
            constexpr int R = 64, C = 64;
            std::vector<std::vector<double>> fields;
            for (int k = 0; k < 24; ++k) fields.push_back(slice(R, C, 0.3 * k));
            auto build = [&](int planes, float alpha) {
                auto fig = fig_at(900, 700, 2);
                auto ax = fig->add_subplot3d(1, 1, 1);
                ax->set_view(-55.0, 22.0);
                for (int k = 0; k < planes; ++k) {
                    const double z = -3.0 + 6.0 * k / std::max(1, planes - 1);
                    ax->plane(sextant::PlaneOrientation::XY, z, {.alpha = alpha})
                            ->heatmap(fields[static_cast<std::size_t>(k)], R, C, kExt, kExt,
                                      {.cmap = sextant::Colormap::Viridis});
                }
                return fig;
            };
            for (int k: {1, 2, 4, 8, 16, 24}) {
                auto op = build(k, 1.0f);
                const double o = timed(op);
                auto tl = build(k, 0.5f);
                const double a = timed(tl);
                std::printf("%-8d %10d %12.1f %12.1f %12.1f\n", k, k, o, a, a - o);
            }
        }

        // -- 4c.3  primitive count: one translucent surface, 25^2 .. 200^2 ------
        // The fallback sorts cells and re-uploads indices every frame (237,606
        // indices at 200x200); under peeling this should stay flat.
        std::printf("\n--- 4c.3  primitive count: one surface, 900x700, ss 2 ---\n");
        std::printf("%-10s %10s %12s %12s %12s\n",
                    "samples", "cells", "opaque ms", "alpha ms", "delta ms");
        std::printf("%-10s %10s %12s %12s %12s\n",
                    "----------", "----------", "------------", "------------", "------------"); {
            for (int n: {25, 50, 100, 200}) {
                const std::vector<double> u = grid(n, -4.0, 4.0), v = grid(n, -4.0, 4.0);
                const std::vector<double> h = ripple_on(u, v);
                auto build = [&](float alpha) {
                    auto fig = fig_at(900, 700, 2);
                    auto ax = fig->add_subplot3d(1, 1, 1);
                    ax->set_view(-55.0, 30.0);
                    ax->surface(sextant::PlaneOrientation::XY, u, v, h,
                                {
                                    .colormap = true, .cmap = sextant::Colormap::Viridis,
                                    .alpha = alpha
                                });
                    return fig;
                };
                auto op = build(1.0f);
                const double o = timed(op);
                auto tl = build(0.55f);
                const double a = timed(tl);
                char label[24];
                std::snprintf(label, sizeof(label), "%dx%d", n, n);
                std::printf("%-10s %10d %12.1f %12.1f %12.1f\n",
                            label, (n - 1) * (n - 1), o, a, a - o);
            }
        }

        // -- 4c.4  everything at once -------------------------------------------
        // Every kind translucent and interleaving, two window sizes. Read the
        // absolute numbers against a 16.7 ms frame.
        std::printf("\n--- 4c.4  the whole scene translucent: 4 slices + 2 grids + 2 sheets ---\n");
        std::printf("%-14s %6s %12s %14s %12s\n",
                    "window", "ss", "savefig ms", "target Mpx", "cells");
        std::printf("%-14s %6s %12s %14s %12s\n",
                    "--------------", "------", "------------", "--------------", "------------"); {
            constexpr int R = 64, C = 64;
            const std::vector<double> bu = grid(17, -4.0, 4.0), bv = grid(17, -4.0, 4.0);
            const std::vector<double> bh = ripple_on(bu, bv);
            const std::vector<double> su = grid(100, -4.0, 4.0), sv = grid(100, -4.0, 4.0);
            const std::vector<double> sh = ripple_on(su, sv);
            const int sizes[][2] = {{1280, 800}, {1920, 1200}};
            for (const auto& d: sizes) {
                auto fig = fig_at(d[0], d[1], 2);
                auto ax = fig->add_subplot3d(1, 1, 1);
                ax->set_view(-55.0, 8.0);
                for (int k = 0; k < 4; ++k)
                    ax->plane(sextant::PlaneOrientation::XY, -2.0 + 1.4 * k, {.alpha = 0.45f})
                            ->heatmap(slice(R, C, 0.4 * k), R, C, kExt, kExt,
                                      {.cmap = sextant::Colormap::Viridis});
                ax->bar3d(sextant::PlaneOrientation::XY, bu, bv, bh,
                          {
                              .color = sextant::Color::Orange, .alpha = 0.5f,
                              .width = 0.7f, .depth = 0.7f, .bottom = -3.0
                          });
                ax->bar3d(sextant::PlaneOrientation::YZ, bu, bv, bh,
                          {
                              .color = sextant::Color::Blue, .alpha = 0.5f,
                              .width = 0.5f, .depth = 0.5f, .bottom = -3.0
                          });
                ax->surface(sextant::PlaneOrientation::XY, su, sv, sh,
                            {
                                .colormap = true, .cmap = sextant::Colormap::Viridis,
                                .alpha = 0.5f
                            });
                ax->surface(sextant::PlaneOrientation::ZX, su, sv, sh,
                            {.color = sextant::Color::from_hex(0xff5533), .alpha = 0.5f});
                const double ms = timed(fig);
                char label[32];
                std::snprintf(label, sizeof(label), "%dx%d", d[0], d[1]);
                std::printf("%-14s %6d %12.1f %14.1f %12d\n", label, 2, ms,
                            static_cast<double>(d[0]) * d[1] * 4 / 1.0e6,
                            2 * 99 * 99 + 2 * 16 * 16);
            }
        }

        // -- 4c.5  how many layers a scene actually needs ------------------------
        // The peel can't report running out of layers, so render at a rising
        // count (PngExportOptions::peel_layers) and find where the PNG stops
        // changing. The scene is the gallery's sixth cell (a sheet through
        // translucent bars).
        std::printf("\n--- 4c.5  the peel-layer requirement, by rendering until it stops ---\n"); {
            const std::vector<double> u = grid(13, -3.0, 3.0), v = grid(13, -3.0, 3.0);
            std::vector<double> ripple(u.size() * v.size()), midway(u.size() * v.size());
            for (std::size_t i = 0; i < u.size(); ++i)
                for (std::size_t j = 0; j < v.size(); ++j) {
                    const double r = std::hypot(u[i], v[j]);
                    const std::size_t k = i * v.size() + j;
                    ripple[k] = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
                    midway[k] = -1.5 + 0.5 * ripple[k];
                }
            auto fig = fig_at(500, 475, 1);
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->bar3d(sextant::PlaneOrientation::XY, u, v, ripple,
                      {
                          .color = sextant::Color::Orange, .alpha = 0.5f,
                          .width = 0.7f, .depth = 0.7f, .bottom = -1.5
                      });
            ax->surface(sextant::PlaneOrientation::XY, u, v, midway,
                        {
                            .colormap = true, .cmap = sextant::Colormap::Viridis,
                            .alpha = 0.55f
                        });
            fig->show(false);
            auto bytes = [](const char* p) {
                std::FILE* f = std::fopen(p, "rb");
                std::string s;
                if (!f) return s;
                char buf[65536];
                std::size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
                std::fclose(f);
                return s;
            };
            const int layers[] = {2, 4, 6, 8, 10, 12, 16, 24, 32, 48};
            std::string prev;
            std::printf("%-10s %12s %14s\n", "layers", "PNG KB", "vs previous");
            for (const int L: layers) {
                fig->savefig_png("perf_peel_layers.png", {.peel_layers = L});
                const std::string cur = bytes("perf_peel_layers.png");
                std::printf("%-10d %12.1f %14s\n", L,
                            static_cast<double>(cur.size()) / 1024.0,
                            prev.empty() ? "-" : (cur == prev ? "identical" : "CHANGED"));
                prev = cur;
            }
            fig->close();
            std::printf("The first count whose picture equals the one below it is the\n"
                "requirement; every row after it must say identical.\n");
        }
    }

    // ---------------------------------------------------------------------------
    // 4d. Newell's algorithm — what an exact SVG costs
    // ---------------------------------------------------------------------------
    // SVG export time and file size as primitive count grows (pure CPU). Run twice
    // and compare:
    //
    //     sextant_perf_test svg3d                        (Newell, splitting)
    //     SEXTANT_NEWELL=0 sextant_perf_test svg3d       (the whole-object order)
    //
    // The first two rows (nothing interleaving) are controls and must not move;
    // the rest have a plane cutting through the geometry.
    void bench_newell() {
        std::printf("\n=== 4d. SVG export with an exact painter's order (step 9) ===\n");
        if (const char* env = std::getenv("SEXTANT_NEWELL"))
            std::printf("SEXTANT_NEWELL=%s\n", env);
        else
            std::printf("SEXTANT_NEWELL unset (Newell's algorithm on)\n");
        std::printf("Pure CPU: an SVG export builds no GL context and reads back nothing,\n");
        std::printf("so these times are the painter and the writer and nothing else.\n\n");
        std::printf("%-34s %10s %12s %12s\n", "scene", "polygons", "export ms", "SVG KB");
        std::printf("%-34s %10s %12s %12s\n",
                    "----------------------------------", "----------", "------------", "------------");

        auto grid = [](int n, double lo, double hi) {
            std::vector<double> g(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                g[static_cast<std::size_t>(i)] = lo + (hi - lo) * i / (n - 1);
            return g;
        };
        auto ripple_on = [](const std::vector<double>& u, const std::vector<double>& v) {
            std::vector<double> h(u.size() * v.size());
            for (std::size_t i = 0; i < u.size(); ++i)
                for (std::size_t j = 0; j < v.size(); ++j) {
                    const double r = std::hypot(u[i], v[j]);
                    h[i * v.size() + j] = 1.6 * std::exp(-r / 3.0) * std::cos(r * 1.5);
                }
            return h;
        };
        auto slice = [](int rows, int cols) {
            std::vector<double> m(static_cast<std::size_t>(rows) * cols);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                    m[static_cast<std::size_t>(r) * cols + c] =
                            std::sin(0.3 * c) * std::cos(0.3 * r);
            return m;
        };

        constexpr int W = 900, H = 700;
        auto run = [&](const char* label, int surf_n, int bar_n, bool cut, int polys) {
            sextant::FigureOptions o;
            o.width = W;
            o.height = H;
            o.title = "newell";
            o.vsync = false;
            auto fig = sextant::Figure::create(o);
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->set_view(-55.0, 24.0);
            if (surf_n > 1) {
                const std::vector<double> u = grid(surf_n, -4.0, 4.0), v = grid(surf_n, -4.0, 4.0);
                ax->surface(sextant::PlaneOrientation::XY, u, v, ripple_on(u, v),
                            {.colormap = true, .cmap = sextant::Colormap::Viridis});
            }
            if (bar_n > 1) {
                const std::vector<double> u = grid(bar_n, -4.0, 4.0), v = grid(bar_n, -4.0, 4.0);
                ax->bar3d(sextant::PlaneOrientation::XY, u, v, ripple_on(u, v),
                          {
                              .color = sextant::Color::Orange, .width = 0.7f, .depth = 0.7f,
                              .bottom = -2.0
                          });
            }
            if (cut)
                ax->plane(sextant::PlaneOrientation::YZ, 0.0)
                        ->heatmap(slice(48, 48), 48, 48, {-4.0, 4.0}, {-2.0, 2.0},
                                  {.cmap = sextant::Colormap::Viridis});

            std::vector<double> t;
            for (int rep = 0; rep < 3; ++rep) {
                const auto t0 = Clock::now();
                fig->savefig("perf_newell.svg");
                t.push_back(ms_since(t0));
            }
            std::sort(t.begin(), t.end());
            double kb = 0.0;
            if (FILE* f = std::fopen("perf_newell.svg", "rb")) {
                std::fseek(f, 0, SEEK_END);
                kb = static_cast<double>(std::ftell(f)) / 1024.0;
                std::fclose(f);
            }
            std::printf("%-34s %10d %12.1f %12.1f\n", label, polys, t[1], kb);
        };

        // Controls: nothing may split; time and file must not move between runs.
        run("bar grid 9x9, alone (control)", 0, 9, false, 8 * 8 * 3);
        run("surface 50x50, alone (control)", 50, 0, false, 49 * 49);
        // A plane cutting the geometry, at four sizes.
        run("bar grid 9x9 + a cut", 0, 9, true, 8 * 8 * 3 + 1);
        run("surface 25x25 + a cut", 25, 0, true, 24 * 24 + 1);
        run("surface 50x50 + a cut", 50, 0, true, 49 * 49 + 1);
        run("surface 100x100 + a cut", 100, 0, true, 99 * 99 + 1);
        run("surface 100x100 + grid 17 + a cut", 100, 17, true, 99 * 99 + 16 * 16 * 3 + 1);

        // window_test's test_translucent3d scene at the gallery's size and camera:
        // 288 translucent bars with two sheets running inside them, where the
        // splitting actually happens.
        {
            constexpr int NU = 13, NV = 13, NX = 48, NY = 48;
            std::vector<double> gx(NU), gy(NV), ripple(NU * NV), bowl(NU * NV), midway(NU * NV);
            for (int i = 0; i < NU; ++i) gx[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (NU - 1);
            for (int j = 0; j < NV; ++j) gy[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (NV - 1);
            for (int i = 0; i < NU; ++i)
                for (int j = 0; j < NV; ++j) {
                    const double x = gx[static_cast<std::size_t>(i)];
                    const double y = gy[static_cast<std::size_t>(j)];
                    const double r = std::hypot(x, y);
                    const std::size_t k = static_cast<std::size_t>(i * NV + j);
                    ripple[k] = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
                    bowl[k] = 0.22 * (x * x - y * y);
                    midway[k] = -1.5 + 0.5 * ripple[k];
                }
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
            std::vector<double> gx2(NU), gy2(NV);
            for (int i = 0; i < NU; ++i) gx2[static_cast<std::size_t>(i)] = gx[static_cast<std::size_t>(i)] + 0.25;
            for (int j = 0; j < NV; ++j) gy2[static_cast<std::size_t>(j)] = gy[static_cast<std::size_t>(j)] + 0.25;

            const sextant::Range ext{-3.0, 3.0};
            const sextant::HeatmapOptions kCool{.vmin = -1.2f, .vmax = 5.0f};
            const sextant::HeatmapOptions kWarm{.vmin = -5.0f, .vmax = 1.2f};

            sextant::FigureOptions o;
            o.width = 1500;
            o.height = 950;
            o.title = "newell gallery";
            o.vsync = false;
            auto fig = sextant::Figure::create(o);

            auto c1 = fig->add_subplot3d(2, 3, 1);
            c1->plane(sextant::PlaneOrientation::XY, 0.0, {.alpha = 0.6f})
                    ->heatmap(slice_at(0.0), NY, NX, ext, ext, kCool);
            c1->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
                    ->heatmap(slice_at(1.5), NY, NX, ext, ext, kWarm);

            auto c2 = fig->add_subplot3d(2, 3, 2);
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

            auto c3 = fig->add_subplot3d(2, 3, 3);
            c3->surface(sextant::PlaneOrientation::XY, gx, gy, ripple,
                        {.color = sextant::Color::from_hex(0xff5533), .alpha = 0.55f});
            c3->surface(sextant::PlaneOrientation::XY, gx, gy, bowl,
                        {.color = sextant::Color::from_hex(0x3388ff), .alpha = 0.55f});

            auto c4 = fig->add_subplot3d(2, 3, 4);
            c4->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
                      {
                          .color = sextant::Color::Green, .alpha = 0.5f,
                          .width = 0.8f, .depth = 0.8f, .bottom = -1.5
                      });
            c4->plane(sextant::PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
                    ->heatmap(slice_at(0.6), NY, NX, ext, ext, kWarm);

            auto c5 = fig->add_subplot3d(2, 3, 5);
            c5->surface(sextant::PlaneOrientation::XY, gx, gy, ripple,
                        {.colormap = true, .cmap = sextant::Colormap::Viridis, .alpha = 0.6f});
            c5->plane(sextant::PlaneOrientation::XY, 0.35, {.alpha = 0.6f})
                    ->heatmap(slice_at(0.35), NY, NX, ext, ext, kWarm);

            auto c6 = fig->add_subplot3d(2, 3, 6);
            c6->bar3d(sextant::PlaneOrientation::XY, gx, gy, ripple,
                      {
                          .color = sextant::Color::Orange, .alpha = 0.5f,
                          .width = 0.7f, .depth = 0.7f, .bottom = -1.5
                      });
            c6->surface(sextant::PlaneOrientation::XY, gx, gy, midway,
                        {.colormap = true, .cmap = sextant::Colormap::Viridis, .alpha = 0.55f});

            // savefig_svg() for the report: a timing must say whether the export
            // was exact. SEXTANT_PERF_MAX_SPLITS raises the split bound.
            std::size_t budget = 0;
            if (const char* s = std::getenv("SEXTANT_PERF_MAX_SPLITS"))
                budget = static_cast<std::size_t>(std::strtoull(s, nullptr, 10));

            sextant::SvgSaveReport rep{};
            std::vector<double> t;
            for (int r = 0; r < 3; ++r) {
                const auto t0 = Clock::now();
                rep = fig->savefig_svg("perf_newell_gallery.svg", {.max_splits = budget});
                t.push_back(ms_since(t0));
            }
            std::sort(t.begin(), t.end());
            double kb = 0.0;
            if (FILE* f = std::fopen("perf_newell_gallery.svg", "rb")) {
                std::fseek(f, 0, SEEK_END);
                kb = static_cast<double>(std::ftell(f)) / 1024.0;
                std::fclose(f);
            }
            std::printf("%-34s %10s %12.1f %12.1f\n",
                        "gallery: 6 interleaving cells", "-", t[1], kb);
            std::printf("%-34s worst cell %zu splits, %zu tests -- %s\n", "",
                        rep.splits, rep.tests,
                        rep.scene_order_exact
                            ? "exact"
                            : "GAVE UP (raise SEXTANT_PERF_MAX_SPLITS)");
        }
    }

    // 4. Data-panel editing — interactive, not automatable
    // ---------------------------------------------------------------------------
    void bench_edit() {
        std::printf("\n=== 4. Data panel at scale (interactive) ===\n");
        std::printf("Not automated: the Data panel is toggled from the window's View menu\n");
        std::printf("and there is no public API to open it, so this opens a figure and\n");
        std::printf("leaves the judgement to you.\n\n");
        std::printf("What to check, with a 200k-point line plot loaded:\n");
        std::printf("  1. View > Data Panel on. It should open without a visible stall —\n");
        std::printf("     the table is row-virtualized, so cost should track the window\n");
        std::printf("     height, not the 200k rows behind it.\n");
        std::printf("  2. Drag the table scrollbar from top to bottom. Should stay smooth;\n");
        std::printf("     any hitch means the clipper is being defeated.\n");
        std::printf("  3. Edit one cell near the end of the table and commit it. The plot\n");
        std::printf("     should move immediately.\n");
        std::printf("  4. Change Precision in the panel — it reformats every visible cell.\n");
        std::printf("  5. Toggle the panel back off and confirm ms/frame returns to the\n");
        std::printf("     baseline printed below.\n\n");

        const Series s = make_series(200000);
        auto fig = sextant::Figure::create(perf_opts("perf: data panel (200k points)", 2));
        fig->axes()->line(s.x, s.y);
        fig->axes()->set_title("Data panel scale test - 200k points");

        fig->show(false);
        std::printf("Window open. Sampling the Data-panel-closed baseline for 2 s...\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // warm-up
        const sextant::FrameStats a = fig->frame_stats();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const sextant::FrameStats b = fig->frame_stats();
        if (b.frames > a.frames)
            std::printf("  baseline: %.2f ms/frame\n",
                        (b.total_ms - a.total_ms) / static_cast<double>(b.frames - a.frames));

        std::printf("\nPress ENTER when done...\n");
        std::getchar();
        fig->close();
    }

    // ---------------------------------------------------------------------------
    // 4. Export -- the one path that can price supersampling
    // ---------------------------------------------------------------------------
    // glReadPixels blocks on the GPU, so these include GPU time and the
    // downsample. Context creation and PNG encoding are ss-independent and cancel
    // out of the ss=1 delta.
    void bench_export() {
        std::printf("\n=== 4. savefig() PNG: what supersampling actually costs ===\n");
        std::printf("glReadPixels forces a GPU sync, so these include render + downsample.\n");
        std::printf("'delta' is the cost over ss=1 at the same size — the ~200 ms of\n");
        std::printf("headless GL context creation in every call cancels out of it.\n\n");
        std::printf("%-14s %6s %12s %12s %14s\n",
                    "output", "ss", "savefig ms", "delta ms", "target Mpx");
        std::printf("%-14s %6s %12s %12s %14s\n",
                    "--------------", "------", "------------", "------------", "--------------");

        const Series s = make_series(20000);
        const int sizes[][2] = {{800, 600}, {1920, 1080}, {3840, 2160}};
        for (const auto& d: sizes) {
            double base = 0.0;
            for (int ss: {1, 2, 4}) {
                sextant::FigureOptions o = perf_opts("export", ss);
                o.width = d[0];
                o.height = d[1];
                auto fig = sextant::Figure::create(o);
                fig->axes()->line(s.x, s.y);

                std::vector<double> t;
                for (int rep = 0; rep < 3; ++rep) {
                    const auto t0 = Clock::now();
                    fig->savefig("perf_export.png");
                    t.push_back(ms_since(t0));
                }
                std::sort(t.begin(), t.end());
                const double med = t[1];
                if (ss == 1) base = med;

                char label[32];
                std::snprintf(label, sizeof(label), "%dx%d", d[0], d[1]);
                std::printf("%-14s %6d %12.1f %12.1f %14.1f\n", label, ss, med, med - base,
                            static_cast<double>(d[0]) * d[1] * ss * ss / 1.0e6);
            }
        }
    }

    // savefig() with the figure's window open: shown once, then saved repeatedly
    // (per-call cost). The closed-figure row is the control.
    void bench_export_live() {
        std::printf("\n=== 4b. savefig() with the window open ===\n");
        std::printf("Same figure, saved repeatedly. 'open' should be well under 'closed'\n");
        std::printf("once the export reuses the window thread's context.\n\n");
        std::printf("%-14s %14s %12s %12s\n", "size", "window", "savefig ms", "median of");
        std::printf("%-14s %14s %12s %12s\n", "--------------", "--------------", "------------", "------------");

        const Series s = make_series(20000);
        const int sizes[][2] = {{800, 600}, {1920, 1080}};
        constexpr int kReps = 9;

        // Never-open and after-close take the headless path (controls);
        // after-close also exercises the no-window fallback.
        enum Phase { NeverOpen, Open, AfterClose };
        const struct {
            Phase phase;
            const char* name;
        } phases[] = {
            {NeverOpen, "never open"},
            {Open, "open"},
            {AfterClose, "after close"},
        };

        for (const auto& d: sizes) {
            for (const auto& p: phases) {
                sextant::FigureOptions o = perf_opts("export-live", 2);
                o.width = d[0];
                o.height = d[1];
                auto fig = sextant::Figure::create(o);
                fig->axes()->line(s.x, s.y);
                if (p.phase != NeverOpen) fig->show(false);
                if (p.phase == AfterClose) fig->close();

                // One untimed call first, to build lazy caches.
                fig->savefig("perf_export_live.png");

                std::vector<double> t;
                for (int rep = 0; rep < kReps; ++rep) {
                    const auto t0 = Clock::now();
                    fig->savefig("perf_export_live.png");
                    t.push_back(ms_since(t0));
                }
                std::sort(t.begin(), t.end());

                char label[32];
                std::snprintf(label, sizeof(label), "%dx%d", d[0], d[1]);
                std::printf("%-14s %14s %12.1f %12d\n", label, p.name, t[kReps / 2], kReps);
                if (p.phase == Open) fig->close();
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Interactive -- a live figure you drive by hand, with a running readout
    // ---------------------------------------------------------------------------
    // Whether panning, zooming and resizing feel smooth, with a per-interval
    // readout. Vsync stays on (the real experience); ms/frame excludes the swap.
    void bench_interactive() {
        std::printf("\n=== Interactive: drive the figure yourself ===\n\n");

        const std::string kind = prompt_choice("Plot kind:",
                                               {"line", "scatter", "heatmap"}, "line");
        // Dash style, asked for lines only (the other kinds draw no strokes).
        std::string style_name = "solid";
        if (kind == "line") {
            style_name = prompt_choice("Line style:",
                                       {"solid", "dashed", "dotted", "dashdot"}, "solid");
        }
        const sextant::LineStyle style = line_style_from(style_name);
        const bool dashed = (style != sextant::LineStyle::Solid);

        std::size_t n = 0, rows = 0, cols = 0;
        if (kind == "heatmap") {
            rows = prompt_size("Matrix rows", 1024);
            cols = prompt_size("Matrix cols", 1024);
        } else {
            n = prompt_size("Points", 200000);
        }
        const std::size_t ss = prompt_size("Supersample (1-4)", 2);

        sextant::FigureOptions o = perf_opts("sextant interactive perf", static_cast<int>(ss));
        o.vsync = true; // real interactive feel, not a throughput benchmark
        auto fig = sextant::Figure::create(o);
        auto ax = fig->axes();

        char title[128];
        if (kind == "heatmap") {
            const std::vector<double> m = make_matrix(static_cast<int>(rows), static_cast<int>(cols));
            ax->imshow(m, static_cast<int>(rows), static_cast<int>(cols));
            std::snprintf(title, sizeof(title), "heatmap %zux%zu, supersample %zu", rows, cols, ss);
        } else {
            const Series s = make_series(n);
            if (kind == "scatter") {
                sextant::ScatterOptions so;
                so.size = 6.0f;
                ax->scatter(s.x, s.y, so);
            } else {
                sextant::LineOptions lo;
                lo.linestyle = style;
                // A non-solid run also enables the grid and legend (all three
                // dashing paths); a solid run stays the bare throughput session.
                if (dashed) {
                    lo.name = style_name;
                    ax->line(s.x, s.y, lo);
                    ax->grid(true, {.linestyle = style}).legend();
                } else {
                    ax->line(s.x, s.y, lo);
                }
            }
            std::snprintf(title, sizeof(title), "%s%s%s, %zu points, supersample %zu",
                          kind.c_str(), (kind == "line" ? " " : ""),
                          (kind == "line" ? style_name.c_str() : ""), n, ss);
        }
        ax->set_title(title);

        std::printf("\nOpening: %s\n", title);
        std::printf("Things worth trying:\n");
        std::printf("  - Cosmetic > Navigate on, then drag to pan and scroll to zoom.\n");
        std::printf("    Watch ms/frame while dragging: every pan step republishes the\n");
        std::printf("    snapshot and re-expands the geometry.\n");
        std::printf("  - View > Data Panel on, and scroll it.\n");
        std::printf("  - Resize the window, and drag the Plot/Cosmetic splitter.\n");
        std::printf("  - Double-click the plot to reset the view.\n");
        if (dashed) {
            std::printf("\n  Dashed run — the grid and the legend swatch use the same style,\n");
            std::printf("  so all three dashing paths are on screen at once.\n");
            std::printf("  - Pan, and zoom with the scroll wheel: the dash phase is cached\n");
            std::printf("    across both (a pan leaves the scale alone, a scroll zoom scales\n");
            std::printf("    both axes together), so ms/frame should not move.\n");
            std::printf("  - Now resize the window in ONE direction only, or drag the\n");
            std::printf("    Plot/Cosmetic splitter. That changes the aspect ratio, which is\n");
            std::printf("    the one case that has to rebuild the arc lengths — the only\n");
            std::printf("    place dashing costs anything, and it grows with point count.\n");
            std::printf("  - Check the dashes stay the same size on screen as you zoom in:\n");
            std::printf("    the pattern is measured in pixels, not data units.\n");
        }
        std::printf("\nPress ENTER to finish.\n\n");

        fig->show(false);

        // Wait for ENTER on its own thread so the readout keeps sampling.
        std::atomic<bool> done{false};
        std::thread waiter([&done] {
            std::cin.get();
            done.store(true, std::memory_order_relaxed);
        });

        sextant::FrameStats prev = fig->frame_stats();
        auto prev_t = Clock::now();
        double worst_interval = 0.0;

        while (!done.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (!fig->is_open()) {
                std::printf("(window closed — press ENTER to return)\n");
                break;
            }
            const sextant::FrameStats now = fig->frame_stats();
            const double wall_s = ms_since(prev_t) / 1000.0;
            const unsigned long long df = now.frames - prev.frames;
            if (df > 0) {
                const double ms = (now.total_ms - prev.total_ms) / static_cast<double>(df);
                const double fps = static_cast<double>(df) / wall_s;
                worst_interval = std::max(worst_interval, ms);
                std::printf("  %8llu frames | %7.2f ms/frame | %6.1f FPS | worst so far %.2f ms\n",
                            now.frames, ms, fps, now.max_ms);
            } else {
                std::printf("  (no frames rendered in the last second)\n");
            }
            prev = now;
            prev_t = Clock::now();
        }

        waiter.join();
        const sextant::FrameStats final_stats = fig->frame_stats();
        fig->close();

        std::printf("\nSession summary for %s\n", title);
        if (final_stats.frames > 0) {
            std::printf("  frames rendered   : %llu\n", final_stats.frames);
            std::printf("  mean ms/frame     : %.2f\n",
                        final_stats.total_ms / static_cast<double>(final_stats.frames));
            std::printf("  worst single frame: %.2f ms\n", final_stats.max_ms);
            std::printf("  busiest 1 s window: %.2f ms/frame\n", worst_interval);
        }
        std::printf("  resident memory   : %.1f MB\n", resident_mb());
    }
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep output if a run dies early

    std::printf("sextant performance test\n");
    std::printf("baseline resident: %.1f MB\n\n", resident_mb());

    // An argument wins; otherwise prompt (closed stdin falls through to "all").
    std::string which;
    if (argc > 1) {
        which = argv[1];
    } else {
        which = prompt_choice(
            "Which test?",
            {"all", "ingest", "snapshot", "render", "export", "peel", "svg3d", "edit", "interactive"},
            "all");
        std::printf("\n");
    }

    // "all" skips `edit` and `interactive`, which need a human.
    const bool all = (which == "all");

    if (all || which == "ingest") bench_ingest();
    if (all || which == "snapshot") bench_snapshot();
    if (all || which == "render") bench_render();
    if (all || which == "export") bench_export();
    if (all || which == "export") bench_export_live();
    if (all || which == "peel") bench_peel();
    if (all || which == "svg3d") bench_newell();
    if (which == "edit") bench_edit();
    if (which == "interactive") bench_interactive();

    if (!all && which != "ingest" && which != "snapshot" && which != "render"
        && which != "export" && which != "peel" && which != "svg3d"
        && which != "edit" && which != "interactive") {
        std::printf("unknown test '%s'\n", which.c_str());
        std::printf("usage: sextant_perf_test [all|ingest|snapshot|render|export|peel|svg3d|edit|interactive]\n");
        return 1;
    }

    std::printf("\ndone.\n");
    return 0;
}
