#include "window_thread.h"
#include "figure_export.h"
#include "widgets/imgui_context.h"
#include <chrono>
#include <optional>
#include <utility>

namespace sextant {

WindowThread::WindowThread(FigureOptions opts, RenderFn render_fn, CloseFn on_close)
    : opts_(std::move(opts))
    , render_fn_(std::move(render_fn))
    , on_close_(std::move(on_close))
{}

WindowThread::~WindowThread() {
    stop();
}

void WindowThread::start() {
    running_.store(true);
    thread_ = std::thread([this] { thread_main(); });
    ready_.acquire();  // blocks until window is visible
}

void WindowThread::stop() {
    stop_requested_.store(true);
    if (thread_.joinable()) thread_.join();
}

std::future<WindowThread::ExportResult>
WindowThread::submit_png_export(const FigureSnapshot& snap, std::string path,
                                int width, int height, int supersample) {
    ExportJob job;
    job.snap        = &snap;
    job.path        = std::move(path);
    job.width       = width;
    job.height      = height;
    job.supersample = supersample;
    auto fut = job.result.get_future();

    // Servicing this needs the loop to come around to it, so a submit from
    // the loop's own thread would wait on itself forever. Report it as
    // un-serviced and let the caller use its own context.
    if (std::this_thread::get_id() != loop_thread_id_) {
        std::lock_guard<std::mutex> lock(export_mutex_);
        if (accepting_exports_) {
            export_jobs_.push_back(std::move(job));
            exports_pending_.store(true, std::memory_order_release);
            return fut;
        }
    }

    job.result.set_value(ExportResult{});  // serviced = false
    return fut;
}

void WindowThread::drain_exports(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data) {
    std::vector<ExportJob> jobs;
    {
        std::lock_guard<std::mutex> lock(export_mutex_);
        jobs.swap(export_jobs_);
        exports_pending_.store(false, std::memory_order_relaxed);
    }
    for (auto& job : jobs) {
        ExportResult r;
        r.serviced = true;
        try {
            export_figure_png(ctx, nvg, data, *job.snap, job.path,
                              job.width, job.height, job.supersample);
        } catch (...) {
            r.error = std::current_exception();
        }
        job.result.set_value(std::move(r));
    }
}

void WindowThread::retire_pending_exports() {
    std::vector<ExportJob> jobs;
    {
        std::lock_guard<std::mutex> lock(export_mutex_);
        accepting_exports_ = false;
        jobs.swap(export_jobs_);
        exports_pending_.store(false, std::memory_order_relaxed);
    }
    for (auto& job : jobs)
        job.result.set_value(ExportResult{});  // serviced = false — caller falls back
}

void WindowThread::thread_main() {
    GLContext    ctx({ .width=opts_.width, .height=opts_.height,
                       .title=opts_.title, .visible=true,
                       .resizable=opts_.resizable, .vsync=opts_.vsync });
    NvgRenderer  nvg(ctx.nvg());
    DataRenderer data;
    PlotFbo      plot_fbo;  // lazily sized by render_fn_ on first use

    // Exports get their own DataRenderer rather than borrowing the window's.
    // A savefig at a size the window is not showing has a different
    // CoordTransform, which is part of the cache key for bar outlines and dash
    // arc lengths, so sharing would have each export invalidate them and the
    // next window frame rebuild them. Built on first use.
    std::optional<DataRenderer> export_data;

    // Constructed after GLContext exists, destroyed before it is torn down
    // (reverse local-declaration order) — see ImGuiPanelContext's contract.
    ImGuiPanelContext imgui_ctx(ctx, opts_);

    loop_thread_id_ = std::this_thread::get_id();
    {
        std::lock_guard<std::mutex> lock(export_mutex_);
        accepting_exports_ = true;
    }

    ready_.release();  // unblocks start() — window is now visible

    while (!ctx.should_close() && !stop_requested_.load()) {
        imgui_ctx.make_current();  // this thread's context, never a sibling's

        // Timed region is the render work alone. swap_buffers() below blocks
        // on vsync, so including it would pin every sample near the refresh
        // interval and hide both the actual cost and the remaining headroom.
        const auto t0 = std::chrono::steady_clock::now();
        render_fn_(ctx, nvg, data, plot_fbo);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();

        last_ms_.store(ms, std::memory_order_relaxed);
        total_ms_.store(total_ms_.load(std::memory_order_relaxed) + ms,
                        std::memory_order_relaxed);
        if (ms > max_ms_.load(std::memory_order_relaxed))
            max_ms_.store(ms, std::memory_order_relaxed);
        frames_.fetch_add(1, std::memory_order_relaxed);

        // Outside the timed region above: an export is not render-loop work
        // and charging it to FrameStats would show up as a phantom hitch.
        // Before the swap, so the caller's wait is at most one frame. An
        // atomic load in the common case, and nothing else.
        if (exports_pending_.load(std::memory_order_acquire)) {
            if (!export_data) export_data.emplace();
            drain_exports(ctx, nvg, *export_data);
        }

        ctx.swap_buffers();
        ctx.poll_events();
    }

    // Stop taking work and fulfil anything still queued, before the GL
    // objects above go out of scope — otherwise a caller blocked on its
    // future would wait on a promise nobody can keep.
    retire_pending_exports();

    running_.store(false);
    if (on_close_) on_close_();
}

} // namespace sextant
