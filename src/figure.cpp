#include "sextant/figure.h"
#include "figure_impl.h"
#include "axes_impl.h"
#include "axes3d_impl.h"
#include "window_thread.h"
#include "window_registry.h"
#include "window_broker.h"
#include "event_channel.h"
#include "snapshot_box.h"
#include "edit_box.h"
#include "figure_edits.h"
#include "figure_export.h"
#include "widgets/panel.h"
#include "widgets/panel_state.h"
#include "renderer/gl_context.h"
#include "renderer/render_device.h"
#include "renderer/data_renderer.h"
#include "renderer/figure_layout.h"
#include "widgets/figure_window_shell.h"
#include "platform/platform.h"
#include "messages.h"
#include "output/file_write.h"
#include "output/png_writer.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace sextant {

namespace {

// Pass the export's warning to the message handler, so a caller who ignores
// SvgSaveReport still hears about a misordered picture. Silent when exact.
void warn_if_inexact(std::string_view path, const SvgSaveReport& r) {
    if (r.scene_order_exact || r.warning.empty()) return;
    emit_message(std::string(path) + ": " + r.warning);
}

} // namespace

// -------------------------------------------------------------------------
// Figure::Impl (declared in figure_impl.h)
// -------------------------------------------------------------------------
void Figure::Impl::mark_closed() {
    // Once per show(). The Close event is queued before `open` reads false,
    // so a wait that sees the window closed and then drains delivers it.
    if (registered.exchange(false)) {
        Event e;
        e.kind = EventKind::Close;
        events->push(std::move(e));
        open.store(false);
        unregister_open_window();
    } else {
        open.store(false);
    }
}

Figure::Impl::Slot* Figure::Impl::find_or_reserve_slot(const char* who, int rows, int cols,
                                                       int first, int last) {
    const std::string w(who);
    if (rows <= 0 || cols <= 0)
        throw std::invalid_argument(w + ": rows/cols must be positive");
    if (first < 1 || first > rows * cols || last < 1 || last > rows * cols)
        throw std::invalid_argument(w + ": index out of range");

    // All slots share one grid shape (including the implicit 1x1 from
    // axes()), so overlap can be checked cell by cell.
    const Shape g = grid_shape();
    if (g.rows != 0 && (g.rows != rows || g.cols != cols))
        throw std::invalid_argument(
            w + ": rows/cols must be consistent across calls on the same Figure");
    if (g.rows == 0) check_ratios_for_shape(w, rows, cols);

    const AxesSlot req{ rows, cols, first, last };
    if (req.row1() < req.row0() || req.col1() < req.col0())
        throw std::invalid_argument(
            w + ": a span's last cell must be at or below and right of its first ("
            + describe(first, last) + ")");

    for (auto& s : slots) {
        if (s.index == first && s.last == last) return &s;   // the same request again
        const AxesSlot have{ s.rows, s.cols, s.index, s.last };
        const bool disjoint = req.row1() < have.row0() || have.row1() < req.row0()
                           || req.col1() < have.col0() || have.col1() < req.col0();
        if (!disjoint)
            throw std::invalid_argument(
                w + ": " + describe(first, last)
                + (first == last ? " overlaps" : " overlap") + " the subplot at "
                + describe(s.index, s.last));
    }
    return nullptr;
}

template <class A>
std::shared_ptr<A> Figure::Impl::add_impl(const char* who, const char* other,
                                          int rows, int cols, int first, int last) {
    if (Slot* s = find_or_reserve_slot(who, rows, cols, first, last)) {
        auto p = std::get_if<std::shared_ptr<A>>(&s->axes);
        // A cell holding the other kind throws rather than being replaced.
        if (!p)
            throw std::invalid_argument(
                std::string(who) + ": " + describe(first, last)
                + (first == last ? " already holds a " : " already hold a ")
                + other + " axes");
        return *p;
    }
    auto ax = std::shared_ptr<A>(new A());
    slots.push_back({rows, cols, first, last, ax});
    return ax;
}

std::shared_ptr<Axes> Figure::Impl::add_subplot_impl(int rows, int cols, int first, int last) {
    return add_impl<Axes>("Figure::add_subplot", "3D", rows, cols, first, last);
}
std::shared_ptr<Axes3D> Figure::Impl::add_subplot3d_impl(int rows, int cols, int first, int last) {
    return add_impl<Axes3D>("Figure::add_subplot3d", "2D", rows, cols, first, last);
}

FigureSnapshot Figure::Impl::build_figure_snapshot() const {
    FigureSnapshot fs;
    // Rebuilt from live Axes::Impl, so bump both generations.
    fs.generation      = next_snapshot_generation();
    fs.data_generation = next_snapshot_generation();
    // refresh() always triggers a layout refit.
    fs.layout_generation = next_snapshot_generation();
    fs.col_gap = opts.subplot_col_gap;
    fs.row_gap = opts.subplot_row_gap;
    fs.margins       = opts.margins;
    fs.col_ratios    = col_ratios;
    fs.row_ratios    = row_ratios;
    fs.suptitle      = suptitle_text;
    fs.suptitle_opts = suptitle_opts;
    fs.stamps        = stamps;
    fs.axes.reserve(slots.size());
    for (const auto& s : slots) {
        const AxesSlot slot{ s.rows, s.cols, s.index, s.last };
        if (const Axes3D* a3 = s.as3d())
            fs.axes.push_back({ slot, a3->d->build_snapshot() });
        else
            fs.axes.push_back({ slot, s.as2d()->d->build_snapshot() });
    }
    return fs;
}

void Figure::Impl::fold_ratios(const std::optional<std::vector<float>>& cols,
                               const std::optional<std::vector<float>>& rows) {
    const Shape g = grid_shape();
    const auto fits = [](const std::vector<float>& r, int n) {
        if (r.empty()) return true;
        if (static_cast<int>(r.size()) != n) return false;
        for (float x : r) if (!std::isfinite(x) || x <= 0.0f) return false;
        return true;
    };
    if (cols && g.cols > 0 && fits(*cols, g.cols)) col_ratios = *cols;
    if (rows && g.rows > 0 && fits(*rows, g.rows)) row_ratios = *rows;
}

void Figure::Impl::apply_figure_level(const FigureEdits& e) {
    apply_figure_edits(e, stamps, suptitle_text, suptitle_opts, opts.margins,
                       opts.subplot_col_gap, opts.subplot_row_gap);
}

void Figure::Impl::apply_edits_and_publish() {
    // Journal first: those ops were applied on the render thread before
    // anything still pending, and order matters for structural ops.
    if (auto journal = edit_box.take_journal()) {
        for (const auto& [idx, ops] : journal->per_axes) {
            if (Axes::Impl* d2 = find_slot_impl(idx))
                apply_plot_data_ops(*d2, ops);
            else if (Axes3D::Impl* d3 = find_slot_impl3d(idx))
                apply_plot_data_ops(*d3, ops);   // routed to the named plane
        }
        for (const auto& [idx, t] : journal->titles) {
            if (Axes::Impl* d2 = find_slot_impl(idx))
                apply_title_edits(*d2, t);
            else if (Axes3D::Impl* d3 = find_slot_impl3d(idx))
                apply_title_edits(*d3, t);
        }
        for (const auto& [idx, l] : journal->limits) {
            if (Axes::Impl* d2 = find_slot_impl(idx))
                apply_limit_edits(*d2, l);
            else if (Axes3D::Impl* d3 = find_slot_impl3d(idx))
                apply_limit_edits(*d3, l);
        }
        for (const auto& [idx, c] : journal->cameras)
            if (Axes3D::Impl* d3 = find_slot_impl3d(idx))
                if (d3->camera_stamp <= c.seen) d3->camera = c.camera;
        // The style lane holds no titles, limits, camera or ops, so the
        // whole-edit appliers apply just its styles.
        for (const auto& [idx, e] : journal->styles)
            if (Axes::Impl* d2 = find_slot_impl(idx)) apply_axes_edit(*d2, e);
        for (const auto& [idx, e] : journal->styles3d)
            if (Axes3D::Impl* d3 = find_slot_impl3d(idx)) apply_axes3d_edit(*d3, e);
        apply_figure_level(journal->figure);
        fold_ratios(journal->col_ratios, journal->row_ratios);
    }

    if (auto edits = edit_box.load_and_clear()) {
        for (const auto& [idx, e] : edits->per_axes)
            if (Axes::Impl* d2 = find_slot_impl(idx)) apply_axes_edit(*d2, e);
        for (const auto& [idx, e] : edits->per_axes3d)
            if (Axes3D::Impl* d3 = find_slot_impl3d(idx)) apply_axes3d_edit(*d3, e);
        // Figure-level, outside the per-axes loop.
        apply_figure_level(*edits);
        fold_ratios(edits->col_ratios, edits->row_ratios);
    }
    snapshot_box.store(std::make_shared<const FigureSnapshot>(build_figure_snapshot()));
}

void Figure::Impl::apply_panel_edits_to_snapshot() {
    auto edits = edit_box.load_and_clear_journaled();
    if (!edits) return;
    auto prev = snapshot_box.load();
    if (!prev) return;

    // Shares plot buffers (CowVec); apply_plot_data_ops() clones on write.
    auto next = std::make_shared<FigureSnapshot>(*prev);
    // New content, so a new generation.
    next->generation = next_snapshot_generation();

    // Bump data_generation only for data ops, so pan/zoom keeps the caches.
    bool data_changed = false;
    for (const auto& [idx, e] : edits->per_axes)
        if (!e.plot_ops.empty()) { data_changed = true; break; }
    if (!data_changed)
        for (const auto& [idx, e] : edits->per_axes3d)
            if (!e.plot_ops.empty()) { data_changed = true; break; }
    if (data_changed) next->data_generation = next_snapshot_generation();
    if (!is_navigation_only(*edits)) next->layout_generation = next_snapshot_generation();

    for (const auto& [idx, e] : edits->per_axes) {
        for (auto& fa : next->axes) {
            if (fa.slot.index != idx) continue;
            RenderSnapshot* sp = fa.snap2d();
            if (!sp) break;   // a 3D slot is addressed on the other lane
            apply_axes_edit(*sp, e);
            break;
        }
    }

    for (const auto& [idx, e] : edits->per_axes3d) {
        for (auto& fa : next->axes) {
            if (fa.slot.index != idx) continue;
            if (RenderSnapshot3D* s3 = fa.snap3d()) apply_axes3d_edit(*s3, e);
            break;
        }
    }
    // Figure-level edits, patched on the snapshot (also applied in
    // apply_edits_and_publish()) so they take effect without refresh().
    apply_figure_edits(*edits, next->stamps, next->suptitle, next->suptitle_opts,
                       next->margins, next->col_gap, next->row_gap);
    if (edits->col_ratios)    next->col_ratios    = *edits->col_ratios;
    if (edits->row_ratios)    next->row_ratios    = *edits->row_ratios;
    snapshot_box.store(std::move(next));
}

std::shared_ptr<const FigureMeasure> Figure::Impl::on_screen_measure() const {
    return open.load() ? window_state.plot.layout.load() : nullptr;
}

void Figure::Impl::resolve_live_save_size(Figure& fig, int& w, int& h) {
    if (!open.load()) fig.show(false);
    if (w > 0 && h > 0) return;

    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(1000);
    while (window_state.plot.live_plot_w.load(std::memory_order_relaxed) == 0 &&
           steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(2));
    }
    if (w <= 0) w = window_state.plot.live_plot_w.load(std::memory_order_relaxed);
    if (h <= 0) h = window_state.plot.live_plot_h.load(std::memory_order_relaxed);
    if (w <= 0) w = opts.width;   // window closed again / never rendered a frame
    if (h <= 0) h = opts.height;
}

std::optional<RgbaImage> Figure::Impl::render_via_window(const FigureSnapshot& fsnap,
                                                         int w, int h, int peel_layers,
                                                         const FigureMeasure* on_screen,
                                                         float scale) {
    if (!open.load() || !window_thread) return std::nullopt;
    auto fut = window_thread->submit_rgba_export(fsnap, w, h, opts.supersample,
                                                 peel_layers, on_screen, scale);
    WindowThread::ExportResult r = fut.get();
    if (!r.serviced) return std::nullopt;
    if (r.error) std::rethrow_exception(r.error);
    return std::move(r.image);
}

float Figure::Impl::png_scale(const PngExportOptions& o) const {
    if (!std::isfinite(o.dpi) || o.dpi < 0.0f)
        throw std::invalid_argument("Figure: PngExportOptions::dpi must be 0 "
                                    "(the figure's) or finite and positive");
    return (o.dpi > 0.0f ? o.dpi : opts.dpi) / 96.0f;
}

RgbaImage Figure::Impl::render_rgba(int peel_layers, float scale, int w, int h) {
    ensure_any_axes();
    const FigureSnapshot fsnap = build_figure_snapshot();
    const auto on_screen = on_screen_measure();
    if (auto img = render_via_window(fsnap, w, h, peel_layers, on_screen.get(), scale))
        return std::move(*img);

    GLContext    ctx({ .width=w, .height=h, .title="", .visible=false, .resizable=false,
                       .headless=true });
    RenderDevice dev;   // on ctx, current since its construction
    DataRenderer data;
    return render_figure_rgba(dev, data, fsnap, w, h, opts.supersample,
                              peel_layers, on_screen.get(), scale);
}

SvgRender Figure::Impl::render_svg(const SvgExportOptions& o, int w, int h) {
    ensure_any_axes();
    const FigureSnapshot fsnap = build_figure_snapshot();
    const auto on_screen = on_screen_measure();
    SvgRender r;
    r.svg = render_figure_svg(fsnap, w, h, o, &r.report, on_screen.get());
    return r;
}

Figure::Figure(FigureOptions opts) : d(std::make_unique<Impl>()) {
    // Process-unique, for ImGui::PushID: two figures' widgets never share ids.
    static std::atomic<std::uint64_t> s_next_figure_id{1};
    d->window_state.figure_id = s_next_figure_id.fetch_add(1, std::memory_order_relaxed);
    if (!std::isfinite(opts.dpi) || opts.dpi <= 0.0f)
        throw std::invalid_argument("Figure: FigureOptions::dpi must be finite and positive");
    d->opts = std::move(opts);
        // Normalize supersample once here.
    d->opts.supersample = std::clamp(d->opts.supersample, 1, kMaxSupersample);
}

Figure::~Figure() {
    close();
}

std::shared_ptr<Figure> Figure::create(FigureOptions opts) {
    return std::shared_ptr<Figure>(new Figure(std::move(opts)));
}

std::shared_ptr<Axes> Figure::axes() {
    d->get_or_create_axes();   // throws if slot 1 holds a 3D axes
    return std::get<std::shared_ptr<Axes>>(d->slots.front().axes);
}

std::shared_ptr<Axes> Figure::add_subplot(int rows, int cols, int index) {
    return d->add_subplot_impl(rows, cols, index, index);
}
std::shared_ptr<Axes> Figure::add_subplot(int rows, int cols, SubplotSpan span) {
    return d->add_subplot_impl(rows, cols, span.first, span.last);
}
std::shared_ptr<Axes> Figure::add_subplot(int index) {
    const auto g = d->require_shape("Figure::add_subplot");
    return d->add_subplot_impl(g.rows, g.cols, index, index);
}
std::shared_ptr<Axes> Figure::add_subplot(SubplotSpan span) {
    const auto g = d->require_shape("Figure::add_subplot");
    return d->add_subplot_impl(g.rows, g.cols, span.first, span.last);
}

std::shared_ptr<Axes3D> Figure::add_subplot3d(int rows, int cols, int index) {
    return d->add_subplot3d_impl(rows, cols, index, index);
}
std::shared_ptr<Axes3D> Figure::add_subplot3d(int rows, int cols, SubplotSpan span) {
    return d->add_subplot3d_impl(rows, cols, span.first, span.last);
}
std::shared_ptr<Axes3D> Figure::add_subplot3d(int index) {
    const auto g = d->require_shape("Figure::add_subplot3d");
    return d->add_subplot3d_impl(g.rows, g.cols, index, index);
}
std::shared_ptr<Axes3D> Figure::add_subplot3d(SubplotSpan span) {
    const auto g = d->require_shape("Figure::add_subplot3d");
    return d->add_subplot3d_impl(g.rows, g.cols, span.first, span.last);
}

// -------------------------------------------------------------------------
// show
// -------------------------------------------------------------------------
void Figure::show(bool pause) {
    // A second show() replaces the window. Tear the first one down here, so its
    // close callback cannot land after the new window has opened and report the
    // figure closed.
    close();

    d->ensure_any_axes();

    // Publish the initial snapshot before the render loop starts.
    d->apply_edits_and_publish();

    // render_fn captures `this`; safe because close()/~Figure() join the
    // thread first. It never touches live Axes::Impl.
    auto render_fn = [this](GLContext& ctx, RenderDevice& dev, FigureWindowShell& shell) {
        d->apply_panel_edits_to_snapshot();
        auto snap = d->snapshot_box.load();  // loaded ONCE, reused for both draws below
        // Panel edits pushed this frame record this snapshot's stamps.
        d->edit_box.set_drawn(snap);
        shell.frame(ctx, dev, *snap, d->edit_box, d->window_state);
    };

    auto wt = std::make_unique<WindowThread>(
        d->opts,
        std::move(render_fn),
        [this]{ d->mark_closed(); },
        d->frame_counters
    );

    // Registered before the thread runs, so the close callback always has a
    // registration to take back.
    d->window_state.plot.events = d->events.get();
    d->open.store(true);
    d->registered.store(true);
    register_open_window();

    d->window_thread = std::move(wt);
    try {
        d->window_thread->start();  // blocks until window visible, then returns
    } catch (...) {
        // No window was made: undo the registration without the Close event a
        // window that never opened has no business sending.
        d->window_thread.reset();
        if (d->registered.exchange(false)) unregister_open_window();
        d->open.store(false);
        throw;
    }

    if (pause) {
        std::cout << "Press ENTER to continue (the plot window stays open)..." << std::endl;
        if (pump_runs_here()) {
            // A blocking console read here would starve the window this thread
            // pumps. So pump, and read only once a line (or EOF) is waiting --
            // then the read is whole: a terminal hands over the line at once.
            pump_until([] {
                if (!platform::console_input_ready()) return false;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                return true;
            }, -1.0);
        } else {
            std::cin.get();
        }
    }
}

// -------------------------------------------------------------------------
// close / is_open / refresh
// -------------------------------------------------------------------------
void Figure::close() {
    if (d->window_thread) {
        d->window_thread->stop();   // joins; the close callback has run by now
        d->window_thread.reset();
    }
    // No writer is left, so frame_stats() reads zero from here to the next show().
    d->frame_counters.reset();
    // The render thread handed its window back on its way out. Where this
    // thread is the one that unmakes windows, do it now rather than leave it on
    // screen until whatever poll comes next.
    serve_broker_requests();
    d->mark_closed();
}

bool Figure::is_open() const {
    return d->open.load();
}

namespace {
    // A drain that found nothing to take although something is queued means
    // another thread is mid-drain (or this is a callback waiting on its own
    // figure); a short sleep keeps the caller's wait loop from spinning.
    void drain_or_yield(EventChannel& ch) {
        if (ch.dispatch() == 0 && ch.has_pending())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
} // namespace

bool Figure::wait_closed(double timeout_s) {
    EventChannel& ch = *d->events;
    // Read the flag before draining: the Close event is queued before the flag
    // flips, so a wait that saw "closed" has that event to deliver.
    auto step = [this, &ch] {
        const bool closed = !d->open.load();
        drain_or_yield(ch);
        return closed;
    };

    // On the thread that owns the windows there is nobody else to pump them:
    // blocking here would freeze the very window we are waiting for.
    if (pump_runs_here()) return pump_until(step, timeout_s);

    const bool finite = timeout_s >= 0.0;   // negative or NaN: forever
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(finite ? std::min(timeout_s, 1e9) : 0.0));
    for (;;) {
        if (step()) return true;
        double left = -1.0;
        if (finite) {
            left = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0.0) return !d->open.load();
        }
        // Woken by the close, or by an event to deliver.
        wait_window_closed_or(d->open, left, [&ch] { return ch.has_pending(); });
    }
}

int Figure::connect(EventKind kind, EventCallback callback) {
    if (!callback) throw std::invalid_argument("Figure::connect: empty callback");
    return d->events->connect(kind, std::move(callback));
}

void Figure::disconnect(int id) {
    d->events->disconnect(id);
}

int Figure::dispatch_events() {
    return d->events->dispatch();
}

void Figure::poll_events() {
    // Each window thread polls its own events inside its render loop on Windows
    // and Linux; on a platform whose events belong to the main thread this is
    // the pump, and throws if it is not called there. Either way it then
    // delivers the events the windows have queued, on this thread.
    pump_windows(0.0);
    dispatch_all_events();
}

MessageHandler Figure::set_message_handler(MessageHandler handler) {
    return exchange_message_handler(std::move(handler));
}

void Figure::run() {
    // Same order as wait_closed(): note that no window is open, then drain, so
    // the Close events of the last windows are delivered before this returns.
    auto step = [] {
        const bool none = open_window_count() == 0;
        if (dispatch_all_events() == 0 && any_events_pending())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return none;
    };
    if (pump_runs_here()) {
        pump_until(step, -1.0);
        return;
    }
    while (!step())
        wait_all_windows_closed_or(-1.0, [] { return any_events_pending(); });
}

FrameStats Figure::frame_stats() const {
    // The counters are the Figure's, never the WindowThread's, so this reads
    // nothing close() can free; any thread, during show() or close() alike.
    return d->frame_counters.load();
}

void Figure::refresh() {
    if (!d->open.load()) {
        throw std::logic_error(
            "Figure::refresh(): figure is not open (call show() first, or "
            "the window has already been closed)");
    }
    d->apply_edits_and_publish();
}

// -------------------------------------------------------------------------
// File output
// -------------------------------------------------------------------------
void Figure::savefig(std::string_view path) {
    const auto s   = std::string(path);
    const auto dot = s.rfind('.');
    const auto ext = (dot == std::string::npos) ? "" : s.substr(dot);
    if      (ext == ".png" || ext == ".PNG") savefig_png(path);
    else if (ext == ".svg" || ext == ".SVG") savefig_svg(path);
    else throw std::invalid_argument("savefig: unsupported extension '" + ext + "'");
}

// The savefig_* functions are the render_* ones plus a file write, so a file
// is byte-identical to the in-memory output.
RgbaImage Figure::render_rgba(PngExportOptions opts, int w, int h) {
    d->default_size(w, h);
    const float scale = d->png_scale(opts);
    return d->render_rgba(opts.peel_layers, scale, w, h);
}

std::vector<std::uint8_t> Figure::render_png(PngExportOptions opts, int w, int h) {
    const RgbaImage img = render_rgba(opts, w, h);
    return write_png_to_memory(img.width, img.height, img.pixels);
}

SvgRender Figure::render_svg(SvgExportOptions opts, int w, int h) {
    d->default_size(w, h);
    SvgRender r = d->render_svg(opts, w, h);
    warn_if_inexact("render_svg", r.report);
    return r;
}

void Figure::savefig_png(std::string_view path, PngExportOptions opts, int w, int h) {
    write_file(path, render_png(opts, w, h), "savefig_png");
}

SvgSaveReport Figure::savefig_svg(std::string_view path, SvgExportOptions opts,
                                  int w, int h) {
    d->default_size(w, h);
    SvgRender r = d->render_svg(opts, w, h);
    write_file(path, r.svg, "savefig_svg");
    warn_if_inexact(path, r.report);
    return r.report;
}

void Figure::savefig_png_live(std::string_view path, PngExportOptions opts,
                              int w, int h) {
    const float scale = d->png_scale(opts);
    d->ensure_any_axes();
    d->resolve_live_save_size(*this, w, h);
    // Normally routed to the window thread; the headless fallback lets this be
    // called from any thread.
    const RgbaImage img = d->render_rgba(opts.peel_layers, scale, w, h);
    write_png(path, img.width, img.height, img.pixels);
}

SvgSaveReport Figure::savefig_svg_live(std::string_view path, SvgExportOptions opts,
                                       int w, int h) {
    d->ensure_any_axes();
    d->resolve_live_save_size(*this, w, h);
    SvgRender r = d->render_svg(opts, w, h);
    write_file(path, r.svg, "savefig_svg");
    warn_if_inexact(path, r.report);
    return r.report;
}

void Figure::set_margins(FigureMargins m) {
    d->opts.margins = m;
    d->stamps.margins = next_snapshot_generation();
}

void Figure::set_col_ratios(std::vector<float> ratios) {
    Impl::check_ratios("Figure::set_col_ratios", ratios, d->grid_shape().cols, "columns");
    d->col_ratios = std::move(ratios);
    d->edit_box.discard_ratios(true, false);
}

void Figure::set_row_ratios(std::vector<float> ratios) {
    Impl::check_ratios("Figure::set_row_ratios", ratios, d->grid_shape().rows, "rows");
    d->row_ratios = std::move(ratios);
    d->edit_box.discard_ratios(false, true);
}

std::vector<float> Figure::col_ratios() const { return d->col_ratios; }
std::vector<float> Figure::row_ratios() const { return d->row_ratios; }

void Figure::resize(int width, int height) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Figure::resize: width/height must be positive");
    d->opts.width  = width;
    d->opts.height = height;
    // The window resizes itself on its own thread next frame (GLFW is
    // thread-bound), adding menu bar and panel so the plot area gets the size.
    d->window_state.shell.pending_plot_w.store(width,  std::memory_order_relaxed);
    d->window_state.shell.pending_plot_h.store(height, std::memory_order_relaxed);
}

FigureSize Figure::size_for_frame(int frame_w, int frame_h, int slot_index) const {
    if (frame_w <= 0 || frame_h <= 0)
        throw std::invalid_argument("Figure::size_for_frame: frame size must be positive");
    if (d->slots.empty()) return {};

    const FigureSnapshot fsnap = d->build_figure_snapshot();
    // Use the open window's layout, if any.
    const FigureMeasure m = measure_figure(fsnap, d->on_screen_measure().get());
    const LayoutSize s = figure_size_for_frame(fsnap, m, slot_index,
                                               static_cast<float>(frame_w),
                                               static_cast<float>(frame_h));
    return { static_cast<int>(std::lround(s.width)),
             static_cast<int>(std::lround(s.height)) };
}

void Figure::resize_to_frame(int frame_w, int frame_h, int slot_index) {
    d->ensure_any_axes();
    const FigureSize s = size_for_frame(frame_w, frame_h, slot_index);
    if (s.width > 0 && s.height > 0) resize(s.width, s.height);
}
void Figure::suptitle(std::string_view text, float fontsize) {
    d->suptitle_text = text;
    d->suptitle_opts.fontsize = fontsize;
    d->stamps.suptitle = d->stamps.suptitle_style = next_snapshot_generation();
}
void Figure::set_suptitle_style(SuptitleOptions opts) {
    d->suptitle_opts = opts;
    d->stamps.suptitle_style = next_snapshot_generation();
}

} // namespace sextant
