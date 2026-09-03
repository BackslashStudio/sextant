// sextant -- performance test harness
//
// What large datasets cost in RAM, whether the live window stays smooth, and
// what supersampling and stroke expansion cost at scale.
//
// Usage:  sextant_perf_test [all|ingest|snapshot|render|export|edit|interactive]
//         With no argument it asks at the terminal, falling back to "all" when
//         stdin is closed. "all" covers the automated sections only -- `edit`
//         and `interactive` wait on a human.
//
// Method:
//
//  - Nothing here goes through savefig(). Each call builds its own headless
//    GLContext (~200 ms), which would swamp every figure it is meant to
//    measure. Render cost is sampled from a *live* window via frame_stats().
//  - frame_stats() reports render work excluding the vsync-blocking swap, so
//    "ms/frame" is the real cost and is not clamped at the refresh interval.
//    The separately-reported FPS *is* vsync-capped.
//  - Every measured section discards warm-up frames: the first frames include
//    shader compilation, font atlas upload, FBO allocation and ImGui's
//    dockspace build, none of which recur.
//  - Data is generated from a fixed seed so runs are comparable.

#include <sextant/sextant.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
// NOMINMAX or <windows.h>'s min/max macros break every std::max/std::min call
// below. It still leaks others — rpcndr.h #defines `small`, which is why no
// local here is named that.
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
// Terminal prompts
//
// All of these fall back to their default when stdin is closed or empty
// (a piped/redirected run), so the harness stays scriptable — reading EOF
// must never turn into a hang or a re-prompt loop.
// ---------------------------------------------------------------------------
bool read_line(std::string& out) {
    if (!std::getline(std::cin, out)) return false;
    // Trim, so a stray space or a Windows CR from a piped file doesn't
    // fail an otherwise exact match.
    const auto first = out.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { out.clear(); return true; }
    const auto last = out.find_last_not_of(" \t\r\n");
    out = out.substr(first, last - first + 1);
    return true;
}

std::string prompt_choice(const std::string& question,
                          const std::vector<std::string>& options,
                          const std::string& fallback)
{
    std::printf("%s\n", question.c_str());
    for (std::size_t i = 0; i < options.size(); ++i)
        std::printf("  %zu) %s\n", i + 1, options[i].c_str());

    // Typos re-prompt rather than silently falling back: several of these
    // options run for minutes and open dozens of windows, so quietly starting
    // the wrong one is worse than asking again. EOF still takes the fallback,
    // which is what keeps piped runs working.
    for (;;) {
        std::printf("choice [%s]: ", fallback.c_str());
        std::string line;
        if (!read_line(line)) {              // stdin closed
            std::printf("%s\n", fallback.c_str());   // echo, so piped logs read sensibly
            return fallback;
        }
        if (line.empty()) {
            std::printf("%s\n", fallback.c_str());
            return fallback;
        }
        // Accept either the index or the name itself.
        if (line.size() <= 2 && std::isdigit(static_cast<unsigned char>(line[0]))) {
            const int idx = std::atoi(line.c_str());
            if (idx >= 1 && idx <= static_cast<int>(options.size()))
                return options[static_cast<std::size_t>(idx) - 1];
        }
        for (const auto& o : options)
            if (o == line) return o;

        std::printf("  '%s' is not one of the options.\n", line.c_str());
    }
}

sextant::LineStyle line_style_from(const std::string& name) {
    if (name == "dashed")  return sextant::LineStyle::Dashed;
    if (name == "dotted")  return sextant::LineStyle::Dotted;
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
// Resident memory. Working set (Windows) / RSS (Linux) rather than an
// allocator counter, because the point is what the *process* costs the
// machine, including the copies the library makes internally.
// ---------------------------------------------------------------------------
double resident_mb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    // K32-prefixed form lives in kernel32, so this needs no extra link library.
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
    return 0.0;  // unsupported platform — memory columns will read 0
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
    s.x.reserve(n); s.y.reserve(n); s.z.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) * 0.001;
        s.x.push_back(t);
        // A curve with real direction changes, so the polyline expansion in
        // draw_lines() hits its miter path rather than a straight run.
        s.y.push_back(std::sin(t) * std::cos(t * 0.37) + jitter(rng));
        s.z.push_back(static_cast<double>(i % 1000) / 1000.0);
    }
    return s;
}

std::vector<float> make_matrix(int rows, int cols) {
    std::vector<float> m(static_cast<std::size_t>(rows) * cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            m[static_cast<std::size_t>(r) * cols + c] =
                static_cast<float>(std::sin(r * 0.05) * std::cos(c * 0.05));
    return m;
}

// ---------------------------------------------------------------------------
// Live-window sampling
// ---------------------------------------------------------------------------
// Two different costs, and the distinction matters:
//
//  - `cpu_ms` is what frame_stats() times: the CPU cost of *submitting* the
//    frame. GL calls are asynchronous, so this excludes GPU execution. It is
//    the metric that exposes CPU-side work such as vertex expansion.
//  - `frame_ms` is wall-clock time per frame with vsync off -- the real
//    end-to-end cost, GPU included, and the only one that can see what
//    `supersample` costs. With vsync on it would be pinned at the refresh
//    interval.
//
// A config is GPU-bound where frame_ms >> cpu_ms, CPU-bound where they meet.
struct RenderSample {
    double cpu_ms   = 0.0;
    double frame_ms = 0.0;
    double fps      = 0.0;   // 1000 / frame_ms; vsync is off, so uncapped
    double max_ms   = 0.0;   // worst submission since show()
    unsigned long long frames = 0;
};

// Opens fig, discards warm-up, then samples frame_stats() over `seconds`.
// Differencing two cumulative readings is what makes the warm-up discardable
// without needing any reset on the counters.
RenderSample sample_render(const std::shared_ptr<sextant::Figure>& fig,
                           double seconds = 2.0,
                           unsigned long long warmup_frames = 30)
{
    fig->show(false);   // non-blocking: window runs on its own thread

    // Wait for warm-up frames (bounded, so a window that never renders can't
    // hang the whole run).
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
        r.cpu_ms   = (b.total_ms - a.total_ms) / static_cast<double>(r.frames);
        r.fps      = static_cast<double>(r.frames) / wall_s;
        r.frame_ms = wall_s * 1000.0 / static_cast<double>(r.frames);
    }
    r.max_ms = b.max_ms;   // cumulative worst; warm-up spikes are excluded by
                           // Construction only if they precede `a`, so this is
                           // reported as "worst since show()", not per-interval
    fig->close();
    return r;
}

// Vsync off throughout: see RenderSample for why end-to-end frame cost is
// otherwise unmeasurable.
sextant::FigureOptions perf_opts(const std::string& title, int supersample) {
    sextant::FigureOptions o;
    o.width  = 1280;
    o.height = 800;
    o.title  = title;
    o.supersample = supersample;
    o.vsync = false;
    return o;
}

const std::size_t kSizes[] = { 1000, 10000, 100000, 1000000 };

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

    for (std::size_t n : kSizes) {
        const Series s = make_series(n);
        for (int kind = 0; kind < 2; ++kind) {
            const double before = resident_mb();
            auto fig = sextant::Figure::create(perf_opts("ingest", 1));
            auto ax  = fig->axes();

            const auto t0 = Clock::now();
            if (kind == 0) ax->line(s.x, s.y);
            else           ax->scatter(s.x, s.y);
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

    for (std::size_t n : kSizes) {
        const Series s = make_series(n);
        auto fig = sextant::Figure::create(perf_opts("snapshot", 1));
        auto ax  = fig->axes();
        ax->line(s.x, s.y);
        fig->show(false);

        // One refresh to settle, then time a handful and take the median.
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

    const int supersamples[] = { 1, 2, 4 };

    for (std::size_t n : kSizes) {
        const Series s = make_series(n);
        for (int ss : supersamples) {
            {   // line — exercises build_polyline(): 12*(N-1) floats per frame
                auto fig = sextant::Figure::create(perf_opts("perf line", ss));
                fig->axes()->line(s.x, s.y);
                const RenderSample r = sample_render(fig);
                std::printf("%-10s %10zu %6d %10.2f %10.2f %10.1f\n",
                            "line", n, ss, r.frame_ms, r.cpu_ms, r.fps);
            }
            {   // scatter — instanced, 3 floats per point per frame
                auto fig = sextant::Figure::create(perf_opts("perf scatter", ss));
                sextant::ScatterOptions so; so.size = 6.0f;
                fig->axes()->scatter(s.x, s.y, so);
                const RenderSample r = sample_render(fig);
                std::printf("%-10s %10zu %6d %10.2f %10.2f %10.1f\n",
                            "scatter", n, ss, r.frame_ms, r.cpu_ms, r.fps);
            }
        }
    }

    // Fill-rate control -- included because it establishes a *limitation of
    // this measurement*, not a property of the library. Data is fixed at a
    // trivial size, so anything left scaling with window area x ss^2 would be
    // fragment work. The rows come out flat across a ~200x range of target
    // pixels, which is not physically possible: with vsync off the CPU never
    // blocks on the GPU, so these timings do not contain GPU execution. The
    // `export` section is what actually prices supersampling.
    std::printf("\n--- fill rate CONTROL: trivial data, varying window x supersample ---\n");
    std::printf("Expect flat rows. That is the point: it demonstrates these live\n");
    std::printf("numbers do NOT capture GPU cost, so they cannot price supersampling.\n");
    std::printf("%-14s %6s %10s %10s %14s\n", "window", "ss", "frame ms", "cpu ms", "target Mpx");
    std::printf("%-14s %6s %10s %10s %14s\n", "--------------", "------", "----------", "----------", "--------------");
    // Not named `small`: <windows.h> drags in rpcndr.h, which #defines it.
    const Series tiny = make_series(1000);
    const int wins[][2] = { {640, 480}, {1280, 800}, {1920, 1200}, {2560, 1600} };
    for (const auto& w : wins) {
        for (int ss : { 1, 2, 4 }) {
            sextant::FigureOptions o = perf_opts("perf fill", ss);
            o.width = w[0]; o.height = w[1];
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

    // Heatmap is sized by matrix dimensions, not point count. Separate sweep,
    // because draw_heatmap() re-uploads the whole texture every frame.
    std::printf("\n--- heatmap (matrix re-uploaded per frame by draw_heatmap) ---\n");
    std::printf("%-14s %10s %6s %10s %10s %10s\n", "matrix", "cells", "ss", "frame ms", "cpu ms", "FPS");
    std::printf("%-14s %10s %6s %10s %10s %10s\n", "--------------", "----------", "------", "----------", "----------", "----------");
    const int dims[][2] = { {64, 64}, {256, 256}, {1024, 1024}, {2048, 2048} };
    for (const auto& d : dims) {
        const std::vector<float> m = make_matrix(d[0], d[1]);
        for (int ss : { 1, 2 }) {
            auto fig = sextant::Figure::create(perf_opts("perf heatmap", ss));
            fig->axes()->heatmap(m, d[0], d[1]);
            const RenderSample r = sample_render(fig);
            char label[32];
            std::snprintf(label, sizeof(label), "%dx%d", d[0], d[1]);
            std::printf("%-14s %10d %6d %10.2f %10.2f %10.1f\n",
                        label, d[0] * d[1], ss, r.frame_ms, r.cpu_ms, r.fps);
        }
    }
}

// ---------------------------------------------------------------------------
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
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // warm-up
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
// savefig()'s readback calls glReadPixels, which blocks until the GPU has
// finished, so unlike the live-window numbers these timings do contain GPU
// execution plus the box-filter downsample. Each call also builds its own
// headless GL context and PNG-encodes the result, both independent of the
// supersample factor at a fixed output size, so they cancel out of the ss=1
// delta.
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
    const int sizes[][2] = { {800, 600}, {1920, 1080}, {3840, 2160} };
    for (const auto& d : sizes) {
        double base = 0.0;
        for (int ss : { 1, 2, 4 }) {
            sextant::FigureOptions o = perf_opts("export", ss);
            o.width = d[0]; o.height = d[1];
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

// savefig() with the figure's window already open, which is the case the
// headless path is wasteful in. The figure is shown once and then saved
// repeatedly, so what is measured is the per-call cost rather than one-time
// GLFW/GLAD/font-discovery init.
//
// The closed-figure row is the control: it takes the headless path either way,
// so it must not move.
void bench_export_live() {
    std::printf("\n=== 4b. savefig() with the window open ===\n");
    std::printf("Same figure, saved repeatedly. 'open' should be well under 'closed'\n");
    std::printf("once the export reuses the window thread's context.\n\n");
    std::printf("%-14s %14s %12s %12s\n", "size", "window", "savefig ms", "median of");
    std::printf("%-14s %14s %12s %12s\n", "--------------", "--------------", "------------", "------------");

    const Series s = make_series(20000);
    const int sizes[][2] = { {800, 600}, {1920, 1080} };
    constexpr int kReps = 9;

    // Never-open and after-close both take the headless path and are the
    // controls: they must not move. after-close additionally exercises the
    // fallback — the request finds no window thread to route to and has to
    // stand up its own context, which is the branch a stale `open` flag or a
    // half-torn-down thread would break.
    enum Phase { NeverOpen, Open, AfterClose };
    const struct { Phase phase; const char* name; } phases[] = {
        { NeverOpen,  "never open"  },
        { Open,       "open"        },
        { AfterClose, "after close" },
    };

    for (const auto& d : sizes) {
        for (const auto& p : phases) {
            sextant::FigureOptions o = perf_opts("export-live", 2);
            o.width = d[0]; o.height = d[1];
            auto fig = sextant::Figure::create(o);
            fig->axes()->line(s.x, s.y);
            if (p.phase != NeverOpen) fig->show(false);
            if (p.phase == AfterClose) fig->close();

            // One untimed call first: whichever path it takes, the first one
            // through pays for lazily-built caches that later calls reuse.
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
// The automated sections above sample a *static* window. This one exists for
// the part no benchmark can report: whether it actually feels smooth while you
// pan, zoom and resize. The readout is per-interval rather than cumulative, so
// the number moves in response to what you are doing.
//
// vsync stays ON here, unlike the measured sections: this reproduces the real
// interactive experience, and ms/frame is still the honest cost because
// frame_stats() excludes the swap wait.
void bench_interactive() {
    std::printf("\n=== Interactive: drive the figure yourself ===\n\n");

    const std::string kind = prompt_choice("Plot kind:",
                                           { "line", "scatter", "heatmap" }, "line");
    // Dash style, asked right after the kind so the two "what am
    // I plotting" questions stay together. Only lines have one — scatter and
    // heatmap draw no strokes at all — so the prompt is skipped for those
    // rather than offered and ignored.
    std::string style_name = "solid";
    if (kind == "line") {
        style_name = prompt_choice("Line style:",
                                   { "solid", "dashed", "dotted", "dashdot" }, "solid");
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
    o.vsync = true;   // real interactive feel, not a throughput benchmark
    auto fig = sextant::Figure::create(o);
    auto ax  = fig->axes();

    char title[128];
    if (kind == "heatmap") {
        const std::vector<float> m = make_matrix(static_cast<int>(rows), static_cast<int>(cols));
        ax->heatmap(m, static_cast<int>(rows), static_cast<int>(cols));
        std::snprintf(title, sizeof(title), "heatmap %zux%zu, supersample %zu", rows, cols, ss);
    } else {
        const Series s = make_series(n);
        if (kind == "scatter") {
            sextant::ScatterOptions so; so.size = 6.0f;
            ax->scatter(s.x, s.y, so);
        } else {
            sextant::LineOptions lo;
            lo.linestyle = style;
            // A non-solid run also turns on the grid (same style) and a
            // legend, so one session exercises all three dashing paths —
            // the stroke shader, NanoVG's grid, and the legend swatch.
            // A solid run leaves both off, so it stays the same bare
            // throughput session it has always been and remains comparable
            // with earlier numbers.
            if (dashed) {
                lo.label = style_name;
                ax->line(s.x, s.y, lo);
                ax->grid(true, { .linestyle = style }).legend();
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
    std::printf("  - Controls > Navigate on, then drag to pan and scroll to zoom.\n");
    std::printf("    Watch ms/frame while dragging: every pan step republishes the\n");
    std::printf("    snapshot and re-expands the geometry.\n");
    std::printf("  - View > Data Panel on, and scroll it.\n");
    std::printf("  - Resize the window, and drag the Plot/Controls splitter.\n");
    std::printf("  - Double-click the plot to reset the view.\n");
    if (dashed) {
        std::printf("\n  Dashed run — the grid and the legend swatch use the same style,\n");
        std::printf("  so all three dashing paths are on screen at once.\n");
        std::printf("  - Pan, and zoom with the scroll wheel: the dash phase is cached\n");
        std::printf("    across both (a pan leaves the scale alone, a scroll zoom scales\n");
        std::printf("    both axes together), so ms/frame should not move.\n");
        std::printf("  - Now resize the window in ONE direction only, or drag the\n");
        std::printf("    Plot/Controls splitter. That changes the aspect ratio, which is\n");
        std::printf("    the one case that has to rebuild the arc lengths — the only\n");
        std::printf("    place dashing costs anything, and it grows with point count.\n");
        std::printf("  - Check the dashes stay the same size on screen as you zoom in:\n");
        std::printf("    the pattern is measured in pixels, not data units.\n");
    }
    std::printf("\nPress ENTER to finish.\n\n");

    fig->show(false);

    // ENTER is waited for on its own thread so the readout below can keep
    // sampling; std::cin.get() would otherwise block the whole loop.
    std::atomic<bool> done{false};
    std::thread waiter([&done] { std::cin.get(); done.store(true, std::memory_order_relaxed); });

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
            const double ms  = (now.total_ms - prev.total_ms) / static_cast<double>(df);
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // keep output if a run dies early

    std::printf("sextant performance test\n");
    std::printf("baseline resident: %.1f MB\n\n", resident_mb());

    // Argument wins when given, so scripted runs never stop at a prompt;
    // otherwise ask. With stdin closed the prompt falls through to "all",
    // which keeps `sextant_perf_test < /dev/null` behaving as it did before.
    std::string which;
    if (argc > 1) {
        which = argv[1];
    } else {
        which = prompt_choice(
            "Which test?",
            { "all", "ingest", "snapshot", "render", "export", "edit", "interactive" },
            "all");
        std::printf("\n");
    }

    // "all" runs only the automated sections — `edit` and `interactive` both
    // block on a human, so sweeping them into an unattended run would hang it.
    const bool all = (which == "all");

    if (all || which == "ingest")      bench_ingest();
    if (all || which == "snapshot")    bench_snapshot();
    if (all || which == "render")      bench_render();
    if (all || which == "export")      bench_export();
    if (all || which == "export")      bench_export_live();
    if (which == "edit")               bench_edit();
    if (which == "interactive")        bench_interactive();

    if (!all && which != "ingest" && which != "snapshot" && which != "render"
             && which != "export" && which != "edit" && which != "interactive") {
        std::printf("unknown test '%s'\n", which.c_str());
        std::printf("usage: sextant_perf_test [all|ingest|snapshot|render|export|edit|interactive]\n");
        return 1;
    }

    std::printf("\ndone.\n");
    return 0;
}
