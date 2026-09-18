#include "sextant/figure.h"
#include "axes_impl.h"
#include "axes3d_impl.h"
#include "window_thread.h"
#include "snapshot_box.h"
#include "edit_box.h"
#include "figure_edits.h"
#include "figure_export.h"
#include "widgets/panel.h"
#include "widgets/panel_state.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/figure_layout.h"
#include "renderer/plot_fbo.h"
#include <glad/glad.h>
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace sextant {

namespace {

// The one place an export's own admission that it gave up reaches a caller who
// did not ask for it. `SvgSaveReport` is returned and can be ignored; a wrong
// picture written without a word is the thing this exists to prevent, and a
// line on stderr is the only channel a library has to a caller who ignored the
// return value. It is not a log: nothing is printed on the exact path, which
// is every scene without interpenetrating 3D geometry.
void warn_if_inexact(std::string_view path, const SvgSaveReport& r) {
    if (r.scene_order_exact || r.warning.empty()) return;
    std::cerr << "sextant: " << path << ": " << r.warning << '\n';
}

// Renders one frame. The plot and the controls are both docked ImGui panels:
// the plot goes into an offscreen texture (PlotFbo) sized to the live content
// region of its own dock panel and is displayed via ImGui::Image(), rather
// than being drawn straight into the window. render_frame() itself stays
// unaware that a dockspace exists.
void render_and_composite(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                          PlotFbo& plot_fbo, const FigureSnapshot& snap,
                          const FigureOptions& opts, FigureEditBox& edit_box,
                          PanelState& panel_state)
{
    // Base clear of the real window. The docked Plot (+ Cosmetic, when
    // visible) panels together tile the whole viewport once laid out, but
    // this avoids any undefined-content flash on the very first frame or
    // mid-resize.
    glViewport(0, 0, ctx.width(), ctx.height());
    glClearColor(0.93f, 0.93f, 0.93f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    draw_widget_panel(ctx, nvg, data, plot_fbo, snap, opts, edit_box, panel_state);
}

} // namespace

// -------------------------------------------------------------------------
// Figure::Impl
// -------------------------------------------------------------------------
struct Figure::Impl {
    // A cell holds one kind or the other, never both. The variant mirrors
    // FigureAxesSnapshot's, and for the same reason -- there is no shared
    // vocabulary worth inventing between an Axes and an Axes3D.
    struct Slot {
        int rows, cols, index;
        int last;   // bottom-right cell; == index for a single cell
        std::variant<std::shared_ptr<Axes>, std::shared_ptr<Axes3D>> axes;

        Axes*   as2d() const {
            auto p = std::get_if<std::shared_ptr<Axes>>(&axes);
            return p ? p->get() : nullptr;
        }
        Axes3D* as3d() const {
            auto p = std::get_if<std::shared_ptr<Axes3D>>(&axes);
            return p ? p->get() : nullptr;
        }
    };

    FigureOptions                 opts;
    std::vector<Slot>             slots;
    std::unique_ptr<WindowThread> window_thread;
    std::atomic<bool>             open{false};
    SnapshotBox                   snapshot_box;
    FigureEditBox                 edit_box;
    PanelState                    panel_state;

    std::string     suptitle_text;
    SuptitleOptions suptitle_opts;

    // set_col_ratios()/set_row_ratios(), and dragged boundaries folded back in
    // by refresh() (v1.0 step 15.3). Empty means equal weights.
    std::vector<float> col_ratios, row_ratios;

    // Weights must be finite and positive, and there must be one per track once
    // the grid has a shape. `n` <= 0 means no shape yet, which checks the values
    // only; the call that fixes the shape checks the count.
    static void check_ratios(const std::string& who, const std::vector<float>& r, int n,
                             const char* tracks) {
        for (float x : r)
            if (!std::isfinite(x) || x <= 0.0f)
                throw std::invalid_argument(who + ": every ratio must be finite and positive");
        if (n > 0 && !r.empty() && static_cast<int>(r.size()) != n)
            throw std::invalid_argument(
                who + ": " + std::to_string(r.size()) + " ratios for a grid of "
                + std::to_string(n) + " " + tracks);
    }

    // The ratios set before any subplot, against the shape being fixed now.
    void check_ratios_for_shape(const std::string& who, int rows, int cols) const {
        check_ratios(who, col_ratios, cols, "columns");
        check_ratios(who, row_ratios, rows, "rows");
    }

    // "This figure must have at least one cell to draw" -- which is what
    // show(), savefig() and resize_to_frame() actually need. Deliberately not
    // get_or_create_axes(): that answers the different question "give me the
    // implicit Axes", and throws when slot 1 holds an Axes3D. A figure whose
    // only cell is 3D still has to be showable and saveable.
    void ensure_any_axes() {
        if (slots.empty()) {
            check_ratios_for_shape("Figure", 1, 1);
            slots.push_back({1, 1, 1, 1, std::shared_ptr<Axes>(new Axes())});
        }
    }

    // Sugar for the implicit single-axes case (slot 1,1,1). Anyone who never
    // calls add_subplot() only ever sees this one slot.
    Axes& get_or_create_axes() {
        ensure_any_axes();
        Axes* ax = slots.front().as2d();
        if (!ax)
            throw std::invalid_argument(
                "Figure::axes: this Figure's first cell holds a 3D axes; "
                "reach it through the add_subplot3d() that created it");
        return *ax;
    }

    // The grid shape the first slot fixed, which every later one must share.
    // rows = 0 when nothing has fixed one yet -- the case the shape-less
    // add_subplot(index) and add_subplot({first, last}) refuse.
    struct Shape { int rows = 0, cols = 0; };
    Shape grid_shape() const {
        if (slots.empty()) return {};
        return { slots.front().rows, slots.front().cols };
    }

    static std::string describe(int first, int last) {
        return first == last
            ? "cell " + std::to_string(first)
            : "cells {" + std::to_string(first) + ", " + std::to_string(last) + "}";
    }

    // Shared by every add_subplot() and add_subplot3d(): the grid rules are
    // about the cells, not about what is in them, so a mixed figure obeys
    // exactly the same ones a uniform figure always has. Returns the slot
    // already at exactly these cells, or nullptr when they are free.
    Slot* find_or_reserve_slot(const char* who, int rows, int cols, int first, int last) {
        const std::string w(who);
        if (rows <= 0 || cols <= 0)
            throw std::invalid_argument(w + ": rows/cols must be positive");
        if (first < 1 || first > rows * cols || last < 1 || last > rows * cols)
            throw std::invalid_argument(w + ": index out of range");

        // All slots on a Figure must share the same grid shape — this also
        // rejects mixing axes() (the implicit 1x1 slot) with a later
        // add_subplot() call of a different shape. That is also what rules
        // out two subplots overlapping by being laid out on different grids:
        // with one grid, a cell is either free or someone's.
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

    Shape require_shape(const char* who) const {
        const Shape g = grid_shape();
        if (g.rows == 0)
            throw std::invalid_argument(
                std::string(who) + ": no grid shape yet -- the first call must give "
                "rows and cols");
        return g;
    }

    // A kind of axes (Axes or Axes3D) at the given cells: the one already
    // there, or a new one. `other` names the kind a mismatch would find.
    template <class A>
    std::shared_ptr<A> add_impl(const char* who, const char* other,
                                int rows, int cols, int first, int last) {
        if (Slot* s = find_or_reserve_slot(who, rows, cols, first, last)) {
            auto p = std::get_if<std::shared_ptr<A>>(&s->axes);
            // Re-requesting an occupied cell returns what is in it, but a
            // cell of the other kind is not an A, and silently replacing it
            // would throw away whatever the caller had already put there.
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

    std::shared_ptr<Axes> add_subplot_impl(int rows, int cols, int first, int last) {
        return add_impl<Axes>("Figure::add_subplot", "3D", rows, cols, first, last);
    }
    std::shared_ptr<Axes3D> add_subplot3d_impl(int rows, int cols, int first, int last) {
        return add_impl<Axes3D>("Figure::add_subplot3d", "2D", rows, cols, first, last);
    }

    FigureSnapshot build_figure_snapshot() const {
        FigureSnapshot fs;
        // Rebuilt wholesale from live Axes::Impl, so the data may well have
        // changed — bump both, conservatively.
        fs.generation      = next_snapshot_generation();
        fs.data_generation = next_snapshot_generation();
        // refresh() is a submission, so a refit -- including after the caller
        // set limits, which is navigation only when it comes from the panel.
        fs.layout_generation = next_snapshot_generation();
        fs.col_gap = opts.subplot_col_gap;
        fs.row_gap = opts.subplot_row_gap;
        fs.margins       = opts.margins;
        fs.col_ratios    = col_ratios;
        fs.row_ratios    = row_ratios;
        fs.suptitle      = suptitle_text;
        fs.suptitle_opts = suptitle_opts;
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

    // 2D slots only; find_slot_impl3d() below is the other half. Each answers
    // nullptr for the other kind rather than pretending, which is what lets
    // the two edit lanes stay disjoint.
    Axes::Impl* find_slot_impl(int idx) {
        for (auto& s : slots)
            if (s.index == idx) return s.as2d() ? s.as2d()->d.get() : nullptr;
        return nullptr;
    }

    Axes3D::Impl* find_slot_impl3d(int idx) {
        for (auto& s : slots)
            if (s.index == idx) return s.as3d() ? s.as3d()->d.get() : nullptr;
        return nullptr;
    }

    // A dragged or typed weight vector, onto the authoritative one. One that no
    // longer matches the grid (a figure that was reshaped since) is dropped
    // rather than thrown about: this runs inside refresh(), for an edit the
    // caller never made.
    void fold_ratios(const std::optional<std::vector<float>>& cols,
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

    // Drains pending widget-panel edits into live Axes::Impl (direct field
    // writes, safe because this always runs on the caller thread), then
    // publishes a fresh snapshot. refresh() is a thin wrapper around this.
    void apply_edits_and_publish() {
        // Journal first. These ops were already applied to the *snapshot* by
        // the render thread, at a point strictly before anything still in
        // pending_ -- replaying them out of order against the authoritative
        // arrays would index the wrong elements once a structural op is in the
        // mix. take_journal() is destructive, so each op lands exactly once.
        if (auto journal = edit_box.take_journal()) {
            for (const auto& [idx, ops] : journal->per_axes) {
                if (Axes::Impl* d2 = find_slot_impl(idx))
                    apply_plot_data_ops(*d2, ops);
                else if (Axes3D::Impl* d3 = find_slot_impl3d(idx))
                    apply_plot_data_ops(*d3, ops);   // routed to the named plane
            }
            fold_ratios(journal->col_ratios, journal->row_ratios);
        }

        if (auto edits = edit_box.load_and_clear()) {
            for (const auto& [idx, e] : edits->per_axes) {
                Axes::Impl* dp = find_slot_impl(idx);
                if (!dp) continue;
                auto& d2 = *dp;
                if (e.title)  d2.title  = *e.title;
                if (e.xtitle) d2.xtitle = *e.xtitle;
                if (e.ytitle) d2.ytitle = *e.ytitle;
                if (e.xlim_auto) d2.xlim_auto = *e.xlim_auto;
                if (e.xmin) d2.xmin = *e.xmin;
                if (e.xmax) d2.xmax = *e.xmax;
                if (e.ylim_auto) d2.ylim_auto = *e.ylim_auto;
                if (e.ymin) d2.ymin = *e.ymin;
                if (e.ymax) d2.ymax = *e.ymax;
                if (e.grid_enabled) d2.grid_enabled = *e.grid_enabled;
                if (e.grid_opts) d2.grid_opts = *e.grid_opts;
                if (e.legend_enabled) d2.legend_enabled = *e.legend_enabled;
                if (e.legend_opts) d2.legend_opts = *e.legend_opts;
                if (e.colorbar_opts) d2.colorbar_opts = *e.colorbar_opts;
                if (e.axes_style) d2.axes_style = *e.axes_style;
                if (e.xticks_override)
                    d2.xticks_override = e.xticks_override->empty()
                        ? std::nullopt : std::optional(*e.xticks_override);
                if (e.yticks_override)
                    d2.yticks_override = e.yticks_override->empty()
                        ? std::nullopt : std::optional(*e.yticks_override);
                apply_plot_data_ops(d2, e.plot_ops);
                apply_plot_style_edits(d2, e.plot_styles);
            }
            for (const auto& [idx, e] : edits->per_axes3d)
                if (Axes3D::Impl* d3 = find_slot_impl3d(idx))
                    apply_axes3d_edit(*d3, e);
            // Figure-level, outside the per-axes loop.
            if (edits->suptitle)      suptitle_text = *edits->suptitle;
            if (edits->suptitle_opts) suptitle_opts = *edits->suptitle_opts;
            if (edits->margins)       opts.margins = *edits->margins;
            if (edits->col_gap)       opts.subplot_col_gap = *edits->col_gap;
            if (edits->row_gap)       opts.subplot_row_gap = *edits->row_gap;
            fold_ratios(edits->col_ratios, edits->row_ratios);
        }
        snapshot_box.store(std::make_shared<const FigureSnapshot>(build_figure_snapshot()));
    }

    // Window-thread-safe counterpart to apply_edits_and_publish(), called once
    // per frame from render_fn. Unlike that one it NEVER reads or writes live
    // Axes::Impl: it patches the edited fields onto a copy of the already-
    // published snapshot and republishes that copy. Axes::Impl is exactly what
    // RenderSnapshot exists to keep a background render thread away from, and
    // a caller thread is free to mutate it concurrently.
    //
    // Draining through load_and_clear_journaled() copies the plot-*data* ops
    // aside into the replay journal, so the next caller-thread
    // apply_edits_and_publish() folds them into Axes::Impl and a later
    // refresh() no longer discards them. Everything else applied here is
    // live-preview-only and does not survive a refresh -- see FigureEditBox
    // for why that asymmetry is deliberate.
    void apply_panel_edits_to_snapshot() {
        auto edits = edit_box.load_and_clear_journaled();
        if (!edits) return;
        auto prev = snapshot_box.load();
        if (!prev) return;

        // Copies decoration/limits but only shares the plot vectors (CowVec) —
        // which matters here more than anywhere else, since a pan or zoom
        // reaches this line on every frame of a drag just to change four
        // doubles. apply_plot_data_ops() below clones whichever buffer it
        // actually writes to.
        auto next = std::make_shared<FigureSnapshot>(*prev);
        // New content, so a new generation.
        next->generation = next_snapshot_generation();

        // ...but only bump data_generation if an edit actually carries data
        // ops. A pan or zoom lands here every frame of a drag with nothing
        // but new limits, and must not invalidate the data-keyed caches.
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
                auto& s = *sp;
                if (e.title)  s.title  = *e.title;
                if (e.xtitle) s.xtitle = *e.xtitle;
                if (e.ytitle) s.ytitle = *e.ytitle;
                if (e.xlim_auto) s.xlim_auto = *e.xlim_auto;
                if (e.xmin) s.xmin = *e.xmin;
                if (e.xmax) s.xmax = *e.xmax;
                if (e.ylim_auto) s.ylim_auto = *e.ylim_auto;
                if (e.ymin) s.ymin = *e.ymin;
                if (e.ymax) s.ymax = *e.ymax;
                if (e.grid_enabled) s.grid_enabled = *e.grid_enabled;
                if (e.grid_opts) s.grid_opts = *e.grid_opts;
                if (e.legend_enabled) s.legend_enabled = *e.legend_enabled;
                if (e.legend_opts) s.legend_opts = *e.legend_opts;
                if (e.colorbar_opts) s.colorbar_opts = *e.colorbar_opts;
                if (e.axes_style) s.axes_style = *e.axes_style;
                if (e.xticks_override)
                    s.xticks_override = e.xticks_override->empty()
                        ? std::nullopt : std::optional(*e.xticks_override);
                if (e.yticks_override)
                    s.yticks_override = e.yticks_override->empty()
                        ? std::nullopt : std::optional(*e.yticks_override);
                apply_plot_data_ops(s, e.plot_ops);
                apply_plot_style_edits(s, e.plot_styles);
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
        // Figure-level decoration and geometry — patched on the snapshot
        // itself, not on any axes, and never touching data_generation.
        // Margins and gaps have to be here as well as in
        // apply_edits_and_publish() above, or dragging one would do nothing
        // until the caller thread happened to call refresh().
        if (edits->suptitle)      next->suptitle      = *edits->suptitle;
        if (edits->suptitle_opts) next->suptitle_opts = *edits->suptitle_opts;
        if (edits->margins)       next->margins       = *edits->margins;
        if (edits->col_gap)       next->col_gap       = *edits->col_gap;
        if (edits->row_gap)       next->row_gap       = *edits->row_gap;
        if (edits->col_ratios)    next->col_ratios    = *edits->col_ratios;
        if (edits->row_ratios)    next->row_ratios    = *edits->row_ratios;
        snapshot_box.store(std::move(next));
    }

    // What an open window is laying out with, so an export or an inverse
    // made while it is on screen keeps its axis furniture (v1.0 step 15.2);
    // null with no window, which is a fresh fit.
    std::shared_ptr<const FigureMeasure> on_screen_measure() const {
        return open.load() ? panel_state.layout.load() : nullptr;
    }

    // Target size for the window-based savefig_png()/savefig_svg(): an
    // explicit w/h wins, otherwise whatever the "Plot" panel is currently
    // rendering at. Spawns a non-blocking window first if none is open, since
    // that is the only way to have a live size at all; live_plot_w/h are
    // populated on the first frame after a window becomes visible, so a short
    // poll covers the gap after show(false) returns.
    void resolve_live_save_size(Figure& fig, int& w, int& h) {
        if (!open.load()) fig.show(false);
        if (w > 0 && h > 0) return;

        using namespace std::chrono;
        const auto deadline = steady_clock::now() + milliseconds(1000);
        while (panel_state.live_plot_w.load(std::memory_order_relaxed) == 0 &&
               steady_clock::now() < deadline) {
            std::this_thread::sleep_for(milliseconds(2));
        }
        if (w <= 0) w = panel_state.live_plot_w.load(std::memory_order_relaxed);
        if (h <= 0) h = panel_state.live_plot_h.load(std::memory_order_relaxed);
        if (w <= 0) w = opts.width;   // window closed again / never rendered a frame
        if (h <= 0) h = opts.height;
    }

    // Routes a PNG export onto the window thread when one is running, so it
    // reuses the GL context that thread already owns instead of standing up a
    // fresh GLFW window, GL context, NanoVG context and font atlas per call.
    //
    // Returns false when there is nothing to route to (no window, the window
    // thread has stopped taking work, or this *is* the window thread) and the
    // caller falls back to its own headless context. A genuine failure inside
    // the export is rethrown instead, so the caller cannot mistake one for the
    // other and silently render twice.
    //
    // Blocks until the frame that services it, which is what lets fsnap stay
    // on the caller's stack. It is deliberately the caller's *own* snapshot,
    // freshly built from Axes::Impl rather than the one the render thread is
    // displaying, so savefig() keeps reading Figure::Impl as it always has.
    bool export_png_via_window(const FigureSnapshot& fsnap, std::string_view path,
                               int w, int h, int peel_layers = 0,
                               const FigureMeasure* on_screen = nullptr) {
        if (!open.load() || !window_thread) return false;
        auto fut = window_thread->submit_png_export(fsnap, std::string(path),
                                                    w, h, opts.supersample,
                                                    peel_layers, on_screen);
        WindowThread::ExportResult r = fut.get();
        if (!r.serviced) return false;
        if (r.error) std::rethrow_exception(r.error);
        return true;
    }
};

Figure::Figure(FigureOptions opts) : d(std::make_unique<Impl>()) {
    d->opts = std::move(opts);
    // Normalize once, here, so every consumer of d->opts (the panel, both
    // savefig paths) can use the factor as-is without repeating the clamp.
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
    d->ensure_any_axes();

    // Build the initial snapshot on the caller thread before any render loop
    // starts, so even the first frame never reads live Axes::Impl state.
    // Routed through apply_edits_and_publish() (normally a no-op drain here)
    // for consistency with every later publish.
    d->apply_edits_and_publish();

    // The window always lives on its own background thread. render_fn
    // captures `this` directly, which is safe because close()/~Figure()
    // always stop() and join this thread first. It drains panel edits through
    // apply_panel_edits_to_snapshot(), which never touches live Axes::Impl,
    // so it is safe to run every frame while the caller thread mutates Axes.
    auto render_fn = [this](GLContext& ctx, NvgRenderer& nvg, DataRenderer& data, PlotFbo& plot_fbo) {
        d->apply_panel_edits_to_snapshot();
        auto snap = d->snapshot_box.load();  // loaded ONCE, reused for both draws below
        render_and_composite(ctx, nvg, data, plot_fbo, *snap, d->opts, d->edit_box, d->panel_state);
    };

    d->open.store(true);
    d->window_thread = std::make_unique<WindowThread>(
        d->opts,
        std::move(render_fn),
        [this]{ d->open.store(false); }
    );
    d->window_thread->start();  // blocks until window visible, then returns

    if (pause) {
        std::cout << "Press ENTER to continue (the plot window stays open)..." << std::endl;
        std::cin.get();
    }
}

// -------------------------------------------------------------------------
// close / is_open / refresh
// -------------------------------------------------------------------------
void Figure::close() {
    if (d->window_thread) {
        d->window_thread->stop();
        d->window_thread.reset();
    }
    d->open.store(false);
}

bool Figure::is_open() const {
    return d->open.load();
}

FrameStats Figure::frame_stats() const {
    // Window_thread is reset by close(), so this reports all-zero both before
    // the first show() and after the window is gone, matching the documented
    // contract rather than dangling.
    return d->window_thread ? d->window_thread->stats() : FrameStats{};
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

void Figure::savefig_png(std::string_view path, PngExportOptions opts, int w, int h) {
    if (w <= 0) w = d->opts.width;
    if (h <= 0) h = d->opts.height;

    d->ensure_any_axes();
    const FigureSnapshot fsnap = d->build_figure_snapshot();
    const auto on_screen = d->on_screen_measure();

    // Reuse the window's context when there is one; otherwise stand up a
    // headless one. Same snapshot, same renderers, same FboReadback either
    // way — only where the GL context came from differs.
    if (d->export_png_via_window(fsnap, path, w, h, opts.peel_layers, on_screen.get())) return;

    GLContext    ctx({ .width=w, .height=h, .title="", .visible=false, .resizable=false });
    NvgRenderer  nvg(ctx.nvg());
    DataRenderer data;
    export_figure_png(ctx, nvg, data, fsnap, path, w, h, d->opts.supersample,
                      opts.peel_layers, on_screen.get());
}

SvgSaveReport Figure::savefig_svg(std::string_view path, SvgExportOptions opts,
                                  int w, int h) {
    if (w <= 0) w = d->opts.width;
    if (h <= 0) h = d->opts.height;

    d->ensure_any_axes();
    const FigureSnapshot fsnap = d->build_figure_snapshot();
    const auto on_screen = d->on_screen_measure();
    SvgSaveReport report;
    export_figure_svg(fsnap, path, w, h, opts, &report, on_screen.get());
    warn_if_inexact(path, report);
    return report;
}

void Figure::savefig_png_live(std::string_view path, PngExportOptions opts,
                              int w, int h) {
    d->ensure_any_axes();
    d->resolve_live_save_size(*this, w, h);
    const FigureSnapshot fsnap = d->build_figure_snapshot();
    const auto on_screen = d->on_screen_measure();

    // Resolve_live_save_size() has already opened a window if there wasn't
    // one, so this normally goes to the window thread. The headless fallback
    // below is what makes savefig_png() callable from any thread rather than
    // only the render thread — the window's context is never touched from
    // here, it is the window's own thread that draws.
    if (d->export_png_via_window(fsnap, path, w, h, opts.peel_layers, on_screen.get())) return;

    GLContext    ctx({ .width=w, .height=h, .title="", .visible=false, .resizable=false });
    NvgRenderer  nvg(ctx.nvg());
    DataRenderer data;
    export_figure_png(ctx, nvg, data, fsnap, path, w, h, d->opts.supersample,
                      opts.peel_layers, on_screen.get());
}

SvgSaveReport Figure::savefig_svg_live(std::string_view path, SvgExportOptions opts,
                                       int w, int h) {
    d->ensure_any_axes();
    d->resolve_live_save_size(*this, w, h);
    const FigureSnapshot fsnap = d->build_figure_snapshot();
    const auto on_screen = d->on_screen_measure();
    SvgSaveReport report;
    export_figure_svg(fsnap, path, w, h, opts, &report, on_screen.get());
    warn_if_inexact(path, report);
    return report;
}

void Figure::set_margins(FigureMargins m) { d->opts.margins = m; }

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
    // A live window is resized by its own thread on the next frame — GLFW
    // window operations belong to the thread that created the window, and
    // this call can come from anywhere. draw_widget_panel() consumes the
    // request and adds the menu bar and Cosmetic column on top, so the plot
    // area (not the window frame) ends up the requested size.
    d->panel_state.pending_plot_w.store(width,  std::memory_order_relaxed);
    d->panel_state.pending_plot_h.store(height, std::memory_order_relaxed);
}

FigureSize Figure::size_for_frame(int frame_w, int frame_h, int slot_index) const {
    if (frame_w <= 0 || frame_h <= 0)
        throw std::invalid_argument("Figure::size_for_frame: frame size must be positive");
    if (d->slots.empty()) return {};

    const FigureSnapshot fsnap = d->build_figure_snapshot();
    // With a window open, for the layout it shows (v1.0 step 15.2).
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
}
void Figure::set_suptitle_style(SuptitleOptions opts) { d->suptitle_opts = opts; }

} // namespace sextant
