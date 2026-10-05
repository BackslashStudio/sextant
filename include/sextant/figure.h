#pragma once
#include "export.h"
#include "axes.h"
#include "axes3d.h"
#include "events.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {
    // Color preset for the panel chrome only (not plot rendering); fixed at create().
    enum class PanelTheme { Dark, Light, Classic };

    struct FigureOptions {
        int width = 800;
        int height = 600;
        std::string title = "sextant";
        bool resizable = true;

        // Every size in this library -- width/height, font sizes, line widths,
        // marker sizes, margins -- is a logical pixel, 1/96 inch. The window
        // draws them at the display's scale (sharper on a Retina or 150%
        // display, laid out the same). A PNG is written at dpi / 96 output pixels
        // per logical pixel: 96 (the default) gives an 800x600 file for an
        // 800x600 figure on every machine, 192 twice that. Default for
        // PngExportOptions::dpi; must be finite and positive.
        float dpi = 96.0f;

        // Gap in pixels between subplot cells (a cell includes its decorations).
        float subplot_col_gap = 0.0f;
        float subplot_row_gap = 0.0f;

        // Figure edge to subplot grid; see FigureMargins. Also set_margins().
        FigureMargins margins;

        // Initial width of the docked Cosmetic panel (show() only, never exported).
        float panel_width = 240.0f;

        // Supersampling factor for window and PNG, clamped to [1, kMaxSupersample];
        // 1 disables it. Cost is quadratic. No effect on SVG or panel chrome.
        int supersample = 2;

        // Cap the render loop at the display refresh rate. Turn off only to measure.
        bool vsync = true;

        PanelTheme theme = PanelTheme::Light;
    };

    // A subplot spanning grid cells `first`..`last` (top-left to bottom-right,
    // 1-based, row-major), like matplotlib's subplot(2, 3, (4, 6)).
    struct SubplotSpan {
        int first = 1;
        int last = 1;
    };

    // Figure size in pixels (the plot area, not the window frame). {0,0} means no
    // axes to size against.
    struct FigureSize {
        int width = 0;
        int height = 0;
    };

    // Upper bound for FigureOptions::supersample.
    inline constexpr int kMaxSupersample = 4;

    // Render-loop timing counters, cumulative since show(); difference two reads for
    // an interval. The ms fields exclude the vsync-blocking swap. Relaxed atomics, so
    // `frames` may be one frame out of step with the sums.
    struct FrameStats {
        unsigned long long frames = 0; // frames rendered since show()
        double total_ms = 0.0; // summed render work
        double last_ms = 0.0; // most recent frame
        double max_ms = 0.0; // worst single frame — hitches
    };

    // Export bounds. Interpenetrating 3D geometry is ordered per piece (SVG) or per
    // pixel (PNG), both with a bound; when one is hit the picture is still drawn but
    // partly in plain depth order. The defaults suffice for nearly every scene.

    // SVG: Newell ordering with polygon splits.
    struct SvgExportOptions {
        // Maximum splits. 0 = automatic (`8 * polygons + 64`). This is the bound
        // that binds in practice; SvgSaveReport::splits reports where it stopped.
        std::size_t max_splits = 0;

        // Maximum pairwise comparisons. 0 = 20,000,000. The wall-clock bound.
        std::size_t max_tests = 0;
    };

    // Result of an SVG export. `scene_order_exact == false` means part of the file
    // is misordered; `warning` says so, and is also written into the SVG and stderr.
    struct SvgSaveReport {
        bool scene_order_exact = true;
        std::size_t splits = 0;
        std::size_t tests = 0;
        std::string warning;
    };

    // PNG: translucent geometry is depth-peeled front to back.
    struct PngExportOptions {
        // Depth-peeling layers, 1..64; 0 = 8. Each extra layer costs one geometry
        // pass, but only where a ray crosses that many translucent surfaces.
        int peel_layers = 0;

        // Output resolution for this file; 0 = FigureOptions::dpi. The layout is
        // the same at any dpi; only the pixel count scales (192 = 2x). Must be
        // 0 or finite and positive.
        float dpi = 0.0f;
    };

    // A rendered figure as raw pixels: 8-bit RGBA, rows top to bottom, no
    // padding (pixels.size() == width * height * 4). The pixels a PNG export
    // encodes, so its size follows PngExportOptions::dpi the same way.
    struct RgbaImage {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;
    };

    // Receives sextant's diagnostics (see Figure::set_message_handler()): one
    // message per call, no "sextant: " prefix, no trailing newline.
    using MessageHandler = std::function<void(std::string_view message)>;

    // An SVG document in memory, with the report savefig_svg() would return.
    struct SvgRender {
        std::string svg;
        SvgSaveReport report;
    };

    // Internal (src/figure_impl.h): in-tree code that drives a Figure itself.
    namespace detail { struct FigureAccess; }

    // Threads. A Figure and the Axes, Axes3D and Plane2D it hands out form one
    // object graph that the window thread never touches (it draws a copy
    // published by show()/refresh()). So any thread may call into a graph, and
    // an open window is never a reason to stop, but calls into one graph must
    // not overlap: serialize them yourself, as with a standard container. The
    // exceptions, safe from any thread at any time, even during another call
    // on the same figure: is_open(), wait_closed(), frame_stats(), connect(),
    // disconnect(), dispatch_events(), and the statics set_message_handler() and,
    // where it is allowed, poll_events()/run(). Separate figures are independent.
    //
    // Events. connect() registers a callback for a kind of event (mouse, key,
    // scroll, resize, close). The window thread never calls it: it only queues,
    // and the callbacks run on the thread that drains the queue -- inside
    // poll_events(), wait_closed(), run() or dispatch_events(). So a callback
    // may block, call close(), or use any figure's graph as that thread would
    // (the rule above still applies: not while another thread is in a call on
    // the same graph). A program that never calls one of those never sees an
    // event.
    //
    // Calls that block: show() until the window is up; close() and the
    // destructor until its thread has joined; savefig*()/render_*() for the
    // render, on the window thread when one is open; wait_closed()/run().
    class SEXTANT_API Figure {
    public:
        static std::shared_ptr<Figure> create(FigureOptions opts = {});

        ~Figure();

        // Subplot access. The first call with rows/cols (or axes(), 1x1) fixes the
        // grid; a different shape later throws, as does a shape-less call before one
        // is fixed. Cells are 1-based, row-major; a span {first, last} is addressed
        // afterwards by its first cell. Returns the subplot already at exactly that
        // cell/span or creates one; any other request touching an occupied cell throws.
        std::shared_ptr<Axes> axes();

        std::shared_ptr<Axes> add_subplot(int rows, int cols, int index);

        std::shared_ptr<Axes> add_subplot(int rows, int cols, SubplotSpan span);

        std::shared_ptr<Axes> add_subplot(int index);

        std::shared_ptr<Axes> add_subplot(SubplotSpan span);

        // 3D axes on the same grid, same rules. A cell holding the other kind throws.
        std::shared_ptr<Axes3D> add_subplot3d(int rows, int cols, int index);

        std::shared_ptr<Axes3D> add_subplot3d(int rows, int cols, SubplotSpan span);

        std::shared_ptr<Axes3D> add_subplot3d(int index);

        std::shared_ptr<Axes3D> add_subplot3d(SubplotSpan span);

        // Display. The window always runs on its own background thread.
        // pause: block the caller until ENTER is pressed on the console. The window
        // closes when this Figure is destroyed or close() is called. Use
        // wait_closed() to wait for the window itself, with no console in it.
        //
        // On macOS, where the window's events belong to the main thread, a
        // blocking console read there would freeze the plot it just opened:
        // called on the main thread, pause keeps pumping the window while it
        // waits for ENTER. Closing the window does not end the wait, as on
        // every other platform.
        void show(bool pause = true);

        void close();

        bool is_open() const;

        // Block until this figure's window has closed, or until timeout_s seconds
        // have passed; a negative timeout waits forever. Returns true once closed
        // (at once if the figure was never shown), false if the timeout ran out
        // first. Callable from any thread, and from several at once. Delivers this
        // figure's events (see connect()) on the calling thread while it waits,
        // and the Close event before it returns true.
        bool wait_closed(double timeout_s = -1);

        // Pump this process's window events once, deliver every figure's queued
        // events (see connect()) on this thread, and return. On Windows and Linux
        // every window pumps its own events on its own thread, so there the pump
        // is a no-op and the delivery works from any thread; call it in a loop
        // that keeps a window up, so the loop stays portable. On macOS it is the
        // pump, and has to be called on the main thread -- anywhere else it
        // throws std::logic_error.
        static void poll_events();

        // Block until every open figure in this process has closed, delivering
        // events meanwhile. Returns at once when none is open (after delivering
        // what is queued).
        static void run();

        // Register `callback` for events of `kind` on this figure; returns an id
        // for disconnect(). Callbacks for a kind run in the order connected. The
        // Event is only valid during the call. An exception a callback throws
        // goes to the message handler and does not stop delivery. The callback
        // is not called for events that predate this call, and events of a kind
        // nobody wants are not queued at all. Throws std::invalid_argument for
        // an empty callback. Events are reported whatever sextant did with the
        // input (Event::consumed says); a callback cannot suppress it.
        int connect(EventKind kind, EventCallback callback);

        // Stops a callback, including for events already queued. Unknown ids are
        // ignored. Callable from inside a callback.
        void disconnect(int id);

        // Deliver this figure's queued events on the calling thread, in order,
        // and return how many there were. What poll_events() does for every
        // figure, for a loop that has its own way of pumping. Returns 0 when
        // called from inside one of this figure's callbacks or while another
        // thread is delivering. A Close event queued by a window that has
        // closed is delivered here too; events still queued when the figure is
        // destroyed are dropped.
        int dispatch_events();

        // Where diagnostics go, process-wide: warnings that do not fail the call
        // that raised them (an SVG whose 3D order is inexact, a fallback to a
        // hidden window, a window still waiting for the main thread's pump).
        // Default, and after an empty handler: stderr, as "sextant: <message>".
        // Returns the previous handler (empty = the default).
        //
        // Thread contract: the handler may be called on any thread -- the one
        // making the call that raised the message, or a figure's window thread --
        // and on several at once. sextant holds none of its locks while calling
        // it, so the handler may block (e.g. take Python's GIL) or call back into
        // sextant, including this function. An exception it throws is ignored.
        static MessageHandler set_message_handler(MessageHandler handler);

        // Publish the current Axes state to the render thread, which keeps drawing
        // meanwhile. Throws std::logic_error before show() or after the window
        // closed.
        void refresh();

        // Headless file output; format from the extension, default options. Use
        // savefig_svg() to get the SvgSaveReport (its warning also goes to stderr).
        void savefig(std::string_view path);

        // Per-format output with options. width/height <= 0 use the Figure's size.
        SvgSaveReport savefig_svg(std::string_view path, SvgExportOptions opts = {},
                                  int width = 0, int height = 0);

        void savefig_png(std::string_view path, PngExportOptions opts = {},
                         int width = 0, int height = 0);

        // In-memory output, for a caller that wants no file. render_png() and
        // render_svg() return exactly the bytes savefig_png()/savefig_svg() write
        // with the same arguments; the savefig_* functions are these plus a file
        // write. render_rgba() is the PNG's pixels before encoding. A
        // render_svg() whose scene order is inexact also warns on stderr.
        std::vector<std::uint8_t> render_png(PngExportOptions opts = {},
                                             int width = 0, int height = 0);

        SvgRender render_svg(SvgExportOptions opts = {}, int width = 0, int height = 0);

        RgbaImage render_rgba(PngExportOptions opts = {}, int width = 0, int height = 0);

        // Figure edge to subplot grid; see FigureMargins.
        void set_margins(FigureMargins margins);

        // Relative column widths / row heights of the subplot grid, e.g. {2, 1};
        // a span gets the sum of its weights. Empty = equal (default). Throws
        // std::invalid_argument for a non-finite/non-positive weight or a count not
        // matching the grid. Dragging a boundary in the window also changes them.
        void set_col_ratios(std::vector<float> ratios);

        void set_row_ratios(std::vector<float> ratios);

        // Current weights; empty when equal.
        std::vector<float> col_ratios() const;

        std::vector<float> row_ratios() const;

        // Resize the plot area (what savefig() writes). An open window grows by its
        // menu bar and panel so the plot lands on the requested size. Also the
        // default size for later headless savefig().
        void resize(int width, int height);

        // Figure size at which subplot `slot_index`'s data frame is frame_w x frame_h,
        // given insets, margins, gaps, suptitle and legend/colorbar. May be off by
        // up to a pixel.
        FigureSize size_for_frame(int frame_w, int frame_h, int slot_index = 1) const;

        // size_for_frame() followed by resize().
        void resize_to_frame(int frame_w, int frame_h, int slot_index = 1);

        // fontsize is in pixels and stored in SuptitleOptions, so a later
        // set_suptitle_style() resets it.
        void suptitle(std::string_view text, float fontsize = 21.0f);

        void set_suptitle_style(SuptitleOptions opts = {});

        // Render-loop timing; all zero before show() and after close(). Any
        // thread, including while another thread is in show() or close().
        FrameStats frame_stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend struct detail::FigureAccess;

        explicit Figure(FigureOptions opts);

        // Export at the live "Plot" panel size (width/height <= 0), opening a window
        // if needed. Currently has no callers.
        void savefig_png_live(std::string_view path, PngExportOptions opts = {},
                              int width = 0, int height = 0);

        SvgSaveReport savefig_svg_live(std::string_view path, SvgExportOptions opts = {},
                                       int width = 0, int height = 0);
    };
} // namespace sextant
