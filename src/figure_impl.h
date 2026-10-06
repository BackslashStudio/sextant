#pragma once
// Internal header — defines Figure::Impl, so in-tree code (layout_test, the
// app built on sextant::internal) can reach the snapshot box, the edit box and
// apply_edits_and_publish() without going through show().
#include "sextant/figure.h"
#include "axes_impl.h"
#include "axes3d_impl.h"
#include "window_thread.h"
#include "event_channel.h"
#include "snapshot_box.h"
#include "edit_box.h"
#include "figure_export.h"
#include "widgets/panel_state.h"
#include <atomic>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace sextant {

struct Figure::Impl {
    // Spellable outside Figure (which is a friend of both); Axes::Impl itself
    // names a private member.
    using AxesImpl   = Axes::Impl;
    using Axes3DImpl = Axes3D::Impl;

    // A cell holds one kind or the other (mirrors FigureAxesSnapshot).
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
    // Declared before window_thread, so it outlives it.
    FrameCounters                 frame_counters;
    // The window thread pushes into it (through window_state.plot.events); the
    // callers' threads connect to it and drain it. Shared with the process-wide
    // list poll_events()/run() walk.
    std::shared_ptr<EventChannel> events = EventChannel::create();
    std::unique_ptr<WindowThread> window_thread;
    std::atomic<bool>             open{false};

    // Paired with the registry's count, once per show(): whichever of the
    // window thread's close callback and close() gets here first does it.
    std::atomic<bool>             registered{false};
    SnapshotBox                   snapshot_box;
    FigureEditBox                 edit_box;
    FigureWindowState             window_state;

    // Retire this figure's registration, from the window thread or the caller,
    // whichever closes it first.
    void mark_closed();

    std::string     suptitle_text;
    SuptitleOptions suptitle_opts;
    // Stamped by the figure-level setters (see FigureStamps).
    FigureStamps    stamps;

    // Grid weights from set_col/row_ratios() and dragged boundaries. Empty = equal.
    std::vector<float> col_ratios, row_ratios;

    // Weights must be finite and positive, one per track. `n` <= 0 (no shape
    // yet) checks values only.
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

    // Validate ratios set before any subplot against the shape fixed now.
    void check_ratios_for_shape(const std::string& who, int rows, int cols) const {
        check_ratios(who, col_ratios, cols, "columns");
        check_ratios(who, row_ratios, rows, "rows");
    }

    // Ensure at least one cell exists (for show/savefig/resize_to_frame).
    // Unlike get_or_create_axes(), accepts a 3D slot 1.
    void ensure_any_axes() {
        if (slots.empty()) {
            check_ratios_for_shape("Figure", 1, 1);
            slots.push_back({1, 1, 1, 1, std::shared_ptr<Axes>(new Axes())});
        }
    }

    // The implicit single-axes case (slot 1,1,1).
    Axes& get_or_create_axes() {
        ensure_any_axes();
        Axes* ax = slots.front().as2d();
        if (!ax)
            throw std::invalid_argument(
                "Figure::axes: this Figure's first cell holds a 3D axes; "
                "reach it through the add_subplot3d() that created it");
        return *ax;
    }

    // The grid shape fixed by the first slot; rows = 0 if none yet.
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

    // Grid rules shared by add_subplot() and add_subplot3d(). Returns the slot
    // at exactly these cells, or nullptr when they are free.
    Slot* find_or_reserve_slot(const char* who, int rows, int cols, int first, int last);

    Shape require_shape(const char* who) const {
        const Shape g = grid_shape();
        if (g.rows == 0)
            throw std::invalid_argument(
                std::string(who) + ": no grid shape yet -- the first call must give "
                "rows and cols");
        return g;
    }

    // The existing axes of kind A at these cells, or a new one. `other` names
    // the kind a mismatch would find. Defined (and instantiated) in figure.cpp.
    template <class A>
    std::shared_ptr<A> add_impl(const char* who, const char* other,
                                int rows, int cols, int first, int last);

    std::shared_ptr<Axes>   add_subplot_impl(int rows, int cols, int first, int last);
    std::shared_ptr<Axes3D> add_subplot3d_impl(int rows, int cols, int first, int last);

    FigureSnapshot build_figure_snapshot() const;

    // 2D slots only; nullptr for a 3D slot (see find_slot_impl3d()).
    AxesImpl* find_slot_impl(int idx) {
        for (auto& s : slots)
            if (s.index == idx) return s.as2d() ? s.as2d()->d.get() : nullptr;
        return nullptr;
    }

    Axes3DImpl* find_slot_impl3d(int idx) {
        for (auto& s : slots)
            if (s.index == idx) return s.as3d() ? s.as3d()->d.get() : nullptr;
        return nullptr;
    }

    // Apply a dragged/typed weight vector; one no longer matching the grid is
    // dropped (this runs inside refresh()).
    void fold_ratios(const std::optional<std::vector<float>>& cols,
                     const std::optional<std::vector<float>>& rows);

    // The figure-level fields of a drained or journaled edit, onto Figure::Impl.
    void apply_figure_level(const FigureEdits& e);

    // Caller thread: drain panel edits into live Axes::Impl, then publish a
    // fresh snapshot.
    void apply_edits_and_publish();

    // Render-thread counterpart: never touches live Axes::Impl. Patches a copy
    // of the published snapshot and republishes it. Every edit is also
    // journaled for the caller thread to replay, so it survives refresh().
    // `inv`, if given, receives what undoes the patch (see patch_snapshot()).
    void apply_panel_edits_to_snapshot(FigureEdits* inv = nullptr);

    // Applies `edits` to a copy of the published snapshot and publishes it;
    // with `inv`, records in it what undoes them, from the snapshot the user
    // saw (GUI-kit R10): only what was actually written, every entry addressed
    // by id, data ops last-first. The journal is the caller's business.
    void patch_snapshot(const FigureEdits& edits, FigureEdits* inv);

    // The start of every frame that draws components, in this order: patch the
    // edits pushed last frame onto the snapshot, load it, and record it as the
    // one this frame's edits are pushed over. Returns that snapshot. Never
    // touches live Axes::Impl, so the window thread runs it too.
    std::shared_ptr<const FigureSnapshot> begin_panel_frame(FigureEdits* inv = nullptr);

    // A host driving this figure on its own thread instead of show(): once
    // per frame, before its components, then draw them from the result. The
    // first call publishes the initial snapshot, as show() does.
    //
    // Publishing is the host's call, never automatic: apply_edits_and_publish()
    // replays the journal into the live axes and rebuilds the snapshot, which
    // bumps every generation (a re-render, data caches dropped), so not every
    // frame. Do it when edit_box.has_journal() and the host's own rule agree
    // (e.g. no ImGui item active), before reading the live axes back, and after
    // changing the figure through the public API (refresh() throws on a figure
    // that is not open). Until then panel edits live only on the snapshot.
    //
    // Stamps: set_drawn() stamps styles, plot styles, plane placements and 3D
    // objects. Titles (title_seen), limits (lim_seen) and the camera
    // (camera_seen) take the drawn snapshot's stamp at the push site, as
    // panel.cpp and plot_view.cpp do; a host pushing those must too, or a
    // setter call made since loses to the edit.
    //
    // Not on a host-driven figure: show(), refresh(), savefig_*_live() (which
    // opens a window); save through perform_save(). One driver at a time:
    // throws std::logic_error while `open`. A host owning the GLFW loop initialises GLFW with
    // ensure_glfw_init() (window_broker.h), not glfwInit().
    //
    // Undo (GUI-kit R10): `inverse`, if given, receives what undoes the edits
    // this frame drained (empty when none), for the host's own undo stack; a
    // gesture spanning frames folds them with compose_inverse().
    std::shared_ptr<const FigureSnapshot> host_frame(FigureEdits* inverse = nullptr);

    // Applies an edit now, outside the edit box, so the edits components push
    // this frame never mix into it: stamps it and fills its ids from the
    // current snapshot (an undo wins over a setter called before it; ids
    // already set are kept), patches the snapshot, journals it as a drain
    // would (publish as after any drain), and returns its inverse. For an undo
    // that is the redo, and the other way round. Host-driven figures only:
    // throws std::logic_error while `open`.
    FigureEdits apply_edits(FigureEdits edits);

    // The open window's layout measurements, so an export keeps them; null
    // without a window.
    std::shared_ptr<const FigureMeasure> on_screen_measure() const;

    // Target size for live exports: explicit w/h, else the Plot panel's live
    // size. Opens a window if needed and polls until the first frame sets it.
    void resolve_live_save_size(Figure& fig, int& w, int& h);

    // Route a raster export to the window thread's GL context, if there is one.
    // Empty when it can't (no window, stopping, or called from that thread) so
    // the caller falls back to headless; real failures rethrow. Blocks until
    // serviced; renders the caller's own fresh snapshot.
    std::optional<RgbaImage> render_via_window(const FigureSnapshot& fsnap,
                                               int w, int h, int peel_layers,
                                               const FigureMeasure* on_screen,
                                               float scale);

    // Output pixels per logical pixel for one PNG: its own dpi, else the
    // figure's, over 96.
    float png_scale(const PngExportOptions& o) const;

    // The raster export every PNG/RGBA entry point shares, at a resolved size:
    // on the window's GL context if there is one, else a headless one.
    RgbaImage render_rgba(int peel_layers, float scale, int w, int h);

    // The SVG export every SVG entry point shares, at a resolved size. Does not
    // warn; each caller does, naming itself.
    SvgRender render_svg(const SvgExportOptions& o, int w, int h);

    void default_size(int& w, int& h) const {
        if (w <= 0) w = opts.width;
        if (h <= 0) h = opts.height;
    }
};

namespace detail {

// The one way in from outside Figure (a friend of it): in-tree code that drives
// a figure itself rather than through show().
struct FigureAccess {
    using Impl = Figure::Impl;   // spellable where Figure::Impl is not
    static Impl&       impl(Figure& f)       { return *f.d; }
    static const Impl& impl(const Figure& f) { return *f.d; }
};

} // namespace detail

} // namespace sextant
