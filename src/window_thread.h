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

// Owns the GLFW window + GL context on a background thread.
// render_fn is called every frame; on_close is called once when the loop exits.
// Thread-safety: start() and stop() must be called from the same (caller) thread.
class WindowThread {
public:
    // plot_fbo is a persistent offscreen render target owned by this thread
    // (constructed once, resized as needed) — used by render_fn to render
    // the plot off-screen and composite it against the widget panel (see
    // spec_widgets.md); render_fn owns the decision of whether to use it.
    using RenderFn = std::function<void(GLContext&, NvgRenderer&, DataRenderer&, PlotFbo&)>;
    using CloseFn  = std::function<void()>;

    WindowThread(FigureOptions opts, RenderFn render_fn, CloseFn on_close);
    ~WindowThread();

    // Launch thread; blocks until window is visible.
    void start();

    // Signal loop to exit and join thread. Safe to call multiple times.
    void stop();

    bool is_running() const { return running_.load(); }

    // Cumulative render-loop timing — see FrameStats in figure.h for the
    // contract (render work only, excludes the vsync-blocking swap; relaxed
    // publication, so the frame count may be one out of step with the sums).
    FrameStats stats() const {
        FrameStats s;
        s.frames   = frames_.load(std::memory_order_relaxed);
        s.total_ms = total_ms_.load(std::memory_order_relaxed);
        s.last_ms  = last_ms_.load(std::memory_order_relaxed);
        s.max_ms   = max_ms_.load(std::memory_order_relaxed);
        return s;
    }

    // Outcome of a PNG export routed onto this thread. `serviced == false`
    // means the request arrived when the loop was no longer taking work (not
    // yet started, already stopping, or submitted from this thread itself) —
    // the caller must fall back to its own headless context rather than treat
    // that as a failure. `error` is only ever set when serviced is true.
    struct ExportResult {
        bool               serviced = false;
        std::exception_ptr error;
    };

    // Render `snap` to a PNG on this window's thread, reusing the GL context
    // it already owns instead of standing up a headless one. Serviced once per
    // frame, just after render_fn_ and outside the FrameStats timing --
    // exporting is not render-loop work.
    //
    // `snap` is borrowed, not copied: it must outlive the returned future, so
    // callers are expected to block on it. It is deliberately not the snapshot
    // the render thread is displaying, which keeps savefig()'s "reads
    // Figure::Impl, so panel edits need a refresh() first" semantics.
    std::future<ExportResult> submit_png_export(const FigureSnapshot& snap,
                                                std::string path,
                                                int width, int height,
                                                int supersample);

private:
    void thread_main();

    // Both run on the window thread. drain_exports() services whatever has
    // been submitted; retire_pending_exports() fulfils anything still queued
    // when the loop exits, as un-serviced, so no caller is left waiting on a
    // broken promise.
    void drain_exports(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data);
    void retire_pending_exports();

    struct ExportJob {
        const FigureSnapshot*      snap = nullptr;
        std::string                path;
        int                        width = 0, height = 0, supersample = 1;
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

    // Written by thread_main() before ready_.release(), read by callers only
    // after start() has returned — the semaphore is the happens-before edge,
    // so no atomic is needed. Exists so a submit from the window thread is
    // rejected instead of deadlocking on its own future.
    std::thread::id        loop_thread_id_{};

    // export_jobs_ and accepting_exports_ are guarded by export_mutex_.
    // exports_pending_ mirrors "export_jobs_ is non-empty" so the frame loop
    // can check for work with an atomic load instead of taking the lock on
    // every frame of every window forever, for something that almost never
    // has anything in it.
    std::mutex             export_mutex_;
    std::vector<ExportJob> export_jobs_;
    bool                   accepting_exports_ = false;
    std::atomic<bool>      exports_pending_{false};
};

} // namespace sextant
