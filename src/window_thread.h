#pragma once
#include "sextant/figure.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/plot_fbo.h"
#include "plot_objects.h"
#include <thread>
#include <semaphore>
#include <atomic>
#include <functional>
#include <exception>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace sextant {
struct FigureMeasure;

// Render-loop timing, written by a window thread, read from any thread. Owned
// by the Figure rather than the WindowThread, so a read never races the
// thread's destruction in close().
struct FrameCounters {
    std::atomic<unsigned long long> frames{0};
    std::atomic<double>             total_ms{0.0};
    std::atomic<double>             last_ms{0.0};
    std::atomic<double>             max_ms{0.0};

    FrameStats load() const {
        FrameStats s;
        s.frames   = frames.load(std::memory_order_relaxed);
        s.total_ms = total_ms.load(std::memory_order_relaxed);
        s.last_ms  = last_ms.load(std::memory_order_relaxed);
        s.max_ms   = max_ms.load(std::memory_order_relaxed);
        return s;
    }

    // Only while no window thread writes (before start(), after stop()).
    void reset() {
        frames.store(0, std::memory_order_relaxed);
        total_ms.store(0.0, std::memory_order_relaxed);
        last_ms.store(0.0, std::memory_order_relaxed);
        max_ms.store(0.0, std::memory_order_relaxed);
    }
};

// Owns the GLFW window + GL context on a background thread.
// render_fn is called every frame; on_close is called once when the loop exits.
// Thread-safety: start() and stop() must be called from the same (caller) thread.
class WindowThread {
public:
    // plot_fbo is a persistent offscreen target owned by this thread, for
    // compositing the plot with the widget panel.
    using RenderFn = std::function<void(GLContext&, NvgRenderer&, DataRenderer&, PlotFbo&)>;
    using CloseFn  = std::function<void()>;

    // `counters` is borrowed and must outlive this object.
    WindowThread(FigureOptions opts, RenderFn render_fn, CloseFn on_close,
                 FrameCounters& counters);
    ~WindowThread();

    // Launch thread; blocks until window is visible. Throws what making the
    // window threw (no display, no GL 4.1), after joining the thread.
    void start();

    // Signal loop to exit and join thread. Safe to call multiple times.
    void stop();

    bool is_running() const { return running_.load(); }

    // `serviced == false`: the loop wasn't taking work (not started, stopping,
    // or submitted from this thread) -- fall back to a headless context.
    // `error` is set only when serviced; `image` only when serviced without one.
    struct ExportResult {
        bool               serviced = false;
        std::exception_ptr error;
        RgbaImage          image;
    };

    // Render `snap` to pixels on this thread's GL context, once per frame after
    // render_fn_ (outside FrameStats); the caller encodes them. `snap` is
    // borrowed and must outlive the future.
    std::future<ExportResult> submit_rgba_export(const FigureSnapshot& snap,
                                                int width, int height,
                                                int supersample,
                                                int peel_layers = 0,
                                                const FigureMeasure* on_screen = nullptr,
                                                float scale = 1.0f);

private:
    void thread_main();
    void run_loop();

    // Window thread only. retire_pending_exports() fulfils leftover jobs as
    // un-serviced when the loop exits.
    void drain_exports(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data);
    void retire_pending_exports();

    struct ExportJob {
        const FigureSnapshot*      snap = nullptr;
        const FigureMeasure*       on_screen = nullptr;   // borrowed, as `snap` is
        int                        width = 0, height = 0, supersample = 1;
        int                        peel_layers = 0;
        float                      scale = 1.0f;          // dpi / 96
        std::promise<ExportResult> result;
    };

    FigureOptions          opts_;
    RenderFn               render_fn_;
    CloseFn                on_close_;
    std::thread            thread_;
    std::binary_semaphore  ready_{0};
    // Set by the thread before the window was up, then ready_ released; start()
    // rethrows it. Written before the release, read after the acquire.
    std::exception_ptr     startup_error_;
    bool                   window_up_ = false; // thread-local use only
    std::atomic<bool>      running_{false};
    std::atomic<bool>      stop_requested_{false};

    FrameCounters&         counters_;

    // Set before ready_.release() (the happens-before edge). Lets a submit from
    // the window thread be rejected instead of deadlocking.
    std::thread::id        loop_thread_id_{};

    // export_jobs_/accepting_exports_ are guarded by export_mutex_;
    // exports_pending_ lets the frame loop check for work without locking.
    std::mutex             export_mutex_;
    std::vector<ExportJob> export_jobs_;
    bool                   accepting_exports_ = false;
    std::atomic<bool>      exports_pending_{false};
};

} // namespace sextant
