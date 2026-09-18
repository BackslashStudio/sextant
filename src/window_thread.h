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

// Owns the GLFW window + GL context on a background thread.
// render_fn is called every frame; on_close is called once when the loop exits.
// Thread-safety: start() and stop() must be called from the same (caller) thread.
class WindowThread {
public:
    // plot_fbo is a persistent offscreen target owned by this thread, for
    // compositing the plot with the widget panel.
    using RenderFn = std::function<void(GLContext&, NvgRenderer&, DataRenderer&, PlotFbo&)>;
    using CloseFn  = std::function<void()>;

    WindowThread(FigureOptions opts, RenderFn render_fn, CloseFn on_close);
    ~WindowThread();

    // Launch thread; blocks until window is visible.
    void start();

    // Signal loop to exit and join thread. Safe to call multiple times.
    void stop();

    bool is_running() const { return running_.load(); }

    // Cumulative render-loop timing; see FrameStats.
    FrameStats stats() const {
        FrameStats s;
        s.frames   = frames_.load(std::memory_order_relaxed);
        s.total_ms = total_ms_.load(std::memory_order_relaxed);
        s.last_ms  = last_ms_.load(std::memory_order_relaxed);
        s.max_ms   = max_ms_.load(std::memory_order_relaxed);
        return s;
    }

    // `serviced == false`: the loop wasn't taking work (not started, stopping,
    // or submitted from this thread) -- fall back to a headless context.
    // `error` is set only when serviced.
    struct ExportResult {
        bool               serviced = false;
        std::exception_ptr error;
    };

    // Render `snap` to PNG on this thread's GL context, once per frame after
    // render_fn_ (outside FrameStats). `snap` is borrowed and must outlive the
    // future.
    std::future<ExportResult> submit_png_export(const FigureSnapshot& snap,
                                                std::string path,
                                                int width, int height,
                                                int supersample,
                                                int peel_layers = 0,
                                                const FigureMeasure* on_screen = nullptr);

private:
    void thread_main();

    // Window thread only. retire_pending_exports() fulfils leftover jobs as
    // un-serviced when the loop exits.
    void drain_exports(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data);
    void retire_pending_exports();

    struct ExportJob {
        const FigureSnapshot*      snap = nullptr;
        const FigureMeasure*       on_screen = nullptr;   // borrowed, as `snap` is
        std::string                path;
        int                        width = 0, height = 0, supersample = 1;
        int                        peel_layers = 0;
        std::promise<ExportResult> result;
    };

    FigureOptions          opts_;
    RenderFn               render_fn_;
    CloseFn                on_close_;
    std::thread            thread_;
    std::binary_semaphore  ready_{0};
    std::atomic<bool>      running_{false};
    std::atomic<bool>      stop_requested_{false};

    std::atomic<unsigned long long> frames_{0};
    std::atomic<double>             total_ms_{0.0};
    std::atomic<double>             last_ms_{0.0};
    std::atomic<double>             max_ms_{0.0};

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
