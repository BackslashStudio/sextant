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
          , on_close_(std::move(on_close)) {
    }

    WindowThread::~WindowThread() {
        stop();
    }

    void WindowThread::start() {
        running_.store(true);
        thread_ = std::thread([this] { thread_main(); });
        ready_.acquire(); // blocks until window is visible
    }

    void WindowThread::stop() {
        stop_requested_.store(true);
        if (thread_.joinable()) thread_.join();
    }

    std::future<WindowThread::ExportResult>
    WindowThread::submit_png_export(const FigureSnapshot& snap, std::string path,
                                    int width, int height, int supersample,
                                    int peel_layers,
                                    const FigureMeasure* on_screen) {
        ExportJob job;
        job.snap = &snap;
        job.on_screen = on_screen;
        job.path = std::move(path);
        job.width = width;
        job.height = height;
        job.supersample = supersample;
        job.peel_layers = peel_layers;
        auto fut = job.result.get_future();

        // A submit from the loop thread would wait on itself; report un-serviced.
        if (std::this_thread::get_id() != loop_thread_id_) {
            std::lock_guard<std::mutex> lock(export_mutex_);
            if (accepting_exports_) {
                export_jobs_.push_back(std::move(job));
                exports_pending_.store(true, std::memory_order_release);
                return fut;
            }
        }

        job.result.set_value(ExportResult{}); // serviced = false
        return fut;
    }

    void WindowThread::drain_exports(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data) {
        std::vector<ExportJob> jobs; {
            std::lock_guard<std::mutex> lock(export_mutex_);
            jobs.swap(export_jobs_);
            exports_pending_.store(false, std::memory_order_relaxed);
        }
        for (auto& job: jobs) {
            ExportResult r;
            r.serviced = true;
            try {
                export_figure_png(ctx, nvg, data, *job.snap, job.path,
                                  job.width, job.height, job.supersample,
                                  job.peel_layers, job.on_screen);
            } catch (...) {
                r.error = std::current_exception();
            }
            job.result.set_value(std::move(r));
        }
    }

    void WindowThread::retire_pending_exports() {
        std::vector<ExportJob> jobs; {
            std::lock_guard<std::mutex> lock(export_mutex_);
            accepting_exports_ = false;
            jobs.swap(export_jobs_);
            exports_pending_.store(false, std::memory_order_relaxed);
        }
        for (auto& job: jobs)
            job.result.set_value(ExportResult{}); // serviced = false — caller falls back
    }

    void WindowThread::thread_main() {
        GLContext ctx({
            .width = opts_.width, .height = opts_.height,
            .title = opts_.title, .visible = true,
            .resizable = opts_.resizable, .vsync = opts_.vsync,
            // Live window only: width/height become physical size
            // (DPI-scaled). savefig() contexts keep exact pixels.
            .scale_to_monitor = true
        });
        NvgRenderer nvg(ctx.nvg());
        DataRenderer data;
        PlotFbo plot_fbo; // lazily sized by render_fn_ on first use

        // Exports get their own DataRenderer (built on first use), so a different
        // export size doesn't invalidate the window's caches.
        std::optional<DataRenderer> export_data;

        // Must be created after and destroyed before GLContext.
        ImGuiPanelContext imgui_ctx(ctx, opts_);

        loop_thread_id_ = std::this_thread::get_id(); {
            std::lock_guard<std::mutex> lock(export_mutex_);
            accepting_exports_ = true;
        }

        ready_.release(); // unblocks start() — window is now visible

        while (!ctx.should_close() && !stop_requested_.load()) {
            imgui_ctx.make_current(); // this thread's context, never a sibling's
            // Before the frame (and after make_current()), so a monitor change
            // applies to this frame.
            imgui_ctx.sync_dpi_scale(ctx);

            // Time render work only; swap_buffers() blocks on vsync.
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

            // Exports run outside the timed region, before the swap.
            if (exports_pending_.load(std::memory_order_acquire)) {
                if (!export_data) export_data.emplace();
                drain_exports(ctx, nvg, *export_data);
            }

            ctx.swap_buffers();
            ctx.poll_events();
        }

        // Fulfil queued exports before the GL objects go out of scope.
        retire_pending_exports();

        running_.store(false);
        if (on_close_) on_close_();
    }
} // namespace sextant
