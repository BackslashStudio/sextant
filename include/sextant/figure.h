#pragma once
#include "export.h"
#include "axes.h"
#include "axes3d.h"
#include <cstddef>
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

        // Currently unused; panel HiDPI scaling comes from the OS.
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
    };

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
        void show(bool pause = true);

        void close();

        bool is_open() const;

        // Block until this figure's window has closed, or until timeout_s seconds
        // have passed; a negative timeout waits forever. Returns true once closed
        // (at once if the figure was never shown), false if the timeout ran out
        // first. Callable from any thread, and from several at once.
        bool wait_closed(double timeout_s = -1);

        // Pump this process's window events once and return. A no-op on Windows
        // and Linux, where every window pumps its own events on its own thread;
        // call it in a loop that keeps a window up, so the loop stays portable.
        static void poll_events();

        // Block until every open figure in this process has closed. Returns at
        // once when none is open.
        static void run();

        // Publish the current Axes state to the render thread. Thread-safe. Throws
        // std::logic_error before show() or after the window closed.
        void refresh();

        // Headless file output; format from the extension, default options. Use
        // savefig_svg() to get the SvgSaveReport (its warning also goes to stderr).
        void savefig(std::string_view path);

        // Per-format output with options. width/height <= 0 use the Figure's size.
        SvgSaveReport savefig_svg(std::string_view path, SvgExportOptions opts = {},
                                  int width = 0, int height = 0);

        void savefig_png(std::string_view path, PngExportOptions opts = {},
                         int width = 0, int height = 0);

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

        // Render-loop timing; all zero before show() and after close(). Call from
        // the thread that drives show()/close().
        FrameStats frame_stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;

        explicit Figure(FigureOptions opts);

        // Export at the live "Plot" panel size (width/height <= 0), opening a window
        // if needed. Currently has no callers.
        void savefig_png_live(std::string_view path, PngExportOptions opts = {},
                              int width = 0, int height = 0);

        SvgSaveReport savefig_svg_live(std::string_view path, SvgExportOptions opts = {},
                                       int width = 0, int height = 0);
    };
} // namespace sextant
