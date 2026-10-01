#include "window_thread.h"
#include "figure_export.h"
#include "widgets/imgui_context.h"
#include "window_broker.h"
#include "platform/platform.h"
#include <chrono>
#include <optional>
#include <utility>

namespace sextant {
    WindowThread::WindowThread(FigureOptions opts, RenderFn render_fn, CloseFn on_close,
                               FrameCounters& counters)
        : opts_(std::move(opts))
          , render_fn_(std::move(render_fn))
          , on_close_(std::move(on_close))
          , counters_(counters) {
    }

    WindowThread::~WindowThread() {
        stop();
    }

    void WindowThread::start() {
        running_.store(true);
        thread_ = std::thread([this] { thread_main(); });

        // The window is made by whichever thread may own one. Where that is this
        // one, waiting here without pumping would be waiting on ourselves: the
        // render thread has just asked us for the window.
        if (pump_runs_here()) {
            while (!ready_.try_acquire()) pump_windows(0.01);
        } else {
            ready_.acquire(); // blocks until window is visible
        }

        if (startup_error_) {
            thread_.join(); // it has returned, or is about to
            std::rethrow_exception(startup_error_);
        }
    }

    void WindowThread::stop() {
        stop_requested_.store(true);
        if (!thread_.joinable()) return;

        // As in start(): where this thread owns the windows (the macOS main
        // thread), a bare join can wait on ourselves. The render thread may be
        // inside a GL call that needs the main thread's run loop -- seen on a GPU-
        // less macOS VM, a window's first glDrawArrays blocked in Apple's software
        // renderer while the main thread sat in join() at interpreter exit. So
        // pump until it has returned, then join.
        if (pump_runs_here()) {
            while (!exited_.load()) pump_windows(0.01);
        }
        thread_.join();
    }

    std::future<WindowThread::ExportResult>
    WindowThread::submit_rgba_export(const FigureSnapshot& snap,
                                     int width, int height, int supersample,
                                     int peel_layers,
                                     const FigureMeasure* on_screen,
                                     float scale) {
        ExportJob job;
        job.snap = &snap;
        job.on_screen = on_screen;
        job.width = width;
        job.height = height;
        job.supersample = supersample;
        job.peel_layers = peel_layers;
        job.scale = scale;
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
                r.image = render_figure_rgba(ctx, nvg, data, *job.snap,
                                             job.width, job.height, job.supersample,
                                             job.peel_layers, job.on_screen, job.scale);
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
        try {
            run_loop();
        } catch (...) {
            // Once the window is up there is no caller left to report to: that
            // stays std::terminate, as before. Until then start() is waiting, so
            // the error is its to throw -- escaping here would end the process
            // (a Python interpreter included) for want of a display.
            if (window_up_) throw;
            startup_error_ = std::current_exception();
            running_.store(false);
            exited_.store(true);
            ready_.release();
            return;
        }
        exited_.store(true);
    }

    void WindowThread::run_loop() {
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

        window_up_ = true;
        ready_.release(); // unblocks start() — window is now visible

        // macOS: one window at a time draws its first frame. On a GPU-less Mac
        // (a CI VM), Apple's software renderer compiles its draw code on a
        // context's first draws, through a cache shared by the process; with
        // several contexts doing that at once, tearing windows down could leave
        // one render thread blocked in that cache for good
        // (cvmRequestFunctionPointerArrayWrite). Teardown while only one window
        // is on its first frame never did. Released after the first swap, or on
        // the way out; a stop() that came while waiting skips the frame.
        std::unique_lock<std::mutex> first_frame;
        if constexpr (platform::windows_on_main_thread) {
            static std::mutex first_frame_mutex;
            first_frame = std::unique_lock<std::mutex>(first_frame_mutex);
        }

        // poll -> requests -> replay -> render -> swap. The poll and the requests
        // are GLContext::poll_events(); the replay is inside the frame, in
        // ImGui_ImplSextant_NewFrame(). It is the order macOS needs, where the
        // first half belongs to the main thread, and the one this loop already
        // had -- the poll moved from after the swap to before the frame it feeds.
        while (!stop_requested_.load()) {
            ctx.poll_events();
            if (ctx.should_close()) break;

            imgui_ctx.make_current(); // this thread's context, never a sibling's
            // Before the frame (and after make_current()), so a monitor change
            // applies to this frame.
            imgui_ctx.sync_dpi_scale(ctx);

            // Held across render and swap: the same lock the window system takes
            // to resize this context's drawable from the pumping thread, so a
            // resize lands between frames instead of inside one. Nothing to take
            // where that cannot happen.
            platform::GLContextLock gl_lock;

            // Time render work only; swap_buffers() blocks on vsync.
            const auto t0 = std::chrono::steady_clock::now();
            render_fn_(ctx, nvg, data, plot_fbo);
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();

            FrameCounters& c = counters_;
            c.last_ms.store(ms, std::memory_order_relaxed);
            c.total_ms.store(c.total_ms.load(std::memory_order_relaxed) + ms,
                             std::memory_order_relaxed);
            if (ms > c.max_ms.load(std::memory_order_relaxed))
                c.max_ms.store(ms, std::memory_order_relaxed);
            c.frames.fetch_add(1, std::memory_order_relaxed);

            // Exports run outside the timed region, before the swap.
            if (exports_pending_.load(std::memory_order_acquire)) {
                if (!export_data) export_data.emplace();
                drain_exports(ctx, nvg, *export_data);
            }

            ctx.swap_buffers();
            if (first_frame.owns_lock()) first_frame.unlock();
        }
        if (first_frame.owns_lock()) first_frame.unlock();

        // One last pump, so a request the final frame posted (a clipboard write
        // from a Ctrl+C, say) is not dropped on the way out.
        ctx.poll_events();

        // Fulfil queued exports before the GL objects go out of scope.
        retire_pending_exports();

        running_.store(false);
        if (on_close_) on_close_();
    }
} // namespace sextant
