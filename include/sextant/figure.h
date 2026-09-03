#pragma once
#include "export.h"
#include "axes.h"
#include <memory>
#include <string>
#include <string_view>

namespace sextant {

// Color preset for the Controls/Plot panel chrome only -- not for plot
// rendering. Chosen at create() time; there is no runtime switching API.
enum class PanelTheme { Dark, Light, Classic };

struct FigureOptions {
    int         width     = 800;
    int         height    = 600;
    std::string title     = "sextant";
    bool        resizable = true;

    // Currently unused. Panel HiDPI scaling comes from the OS/monitor, not
    // from here.
    float       dpi       = 96.0f;

    // Gap in pixels between subplot cells (add_subplot only). A cell is the
    // whole subplot, decorations included, so this separates one subplot.s
    // outermost label from the next one.s.
    float       subplot_col_gap = 0.0f;
    float       subplot_row_gap = 0.0f;

    // Border between the figure's edge and the subplot grid (see
    // FigureMargins in style.h). Also settable later via set_margins(), and
    // live-editable from the Controls panel's Layout section.
    FigureMargins margins;

    // Initial width in pixels of the Controls panel docked to the right edge
    // (interactive show() only; never present in savefig output). Hidden and
    // shown at runtime from the View menu.
    float       panel_width = 240.0f;

    // Antialiasing by supersampling: the plot is rasterized at this multiple
    // of its final size and box-filtered down, identically for the window and
    // for PNG. 1 disables it; clamped to [1, kMaxSupersample]. Quadratic cost
    // (2 means 4x the fragments). Does not affect SVG or the panel chrome.
    int         supersample = 2;

    // Cap the render loop at the display refresh rate. Leave on for
    // interactive use; off exists for measurement, since vsync hides the GPU
    // half of a frame inside the buffer swap.
    bool        vsync = true;

    PanelTheme  theme = PanelTheme::Light;
};

// A figure's pixel size — the plot area, not an open window's outer frame.
// Returned by Figure::size_for_frame(); {0,0} means the figure has no axes
// to size against.
struct FigureSize {
    int width  = 0;
    int height = 0;
};

// Upper bound for FigureOptions::supersample.
inline constexpr int kMaxSupersample = 4;

// Render-loop timing counters, cumulative since show(). Read at two instants
// and difference for an interval:
//
//     mean ms/frame = (total_ms1 - total_ms0) / (frames1 - frames0)
//     achieved FPS  = (frames1 - frames0) / wall_seconds
//
// The millisecond fields measure render work only and exclude the vsync-
// blocking buffer swap, so `frames` over wall time is the achieved rate while
// `total_ms` is what scales with data size. Published with relaxed atomics,
// so a reader may see a frame count one frame out of step with the sums.
struct FrameStats {
    unsigned long long frames   = 0;    // frames rendered since show()
    double             total_ms = 0.0;  // summed render work
    double             last_ms  = 0.0;  // most recent frame
    double             max_ms   = 0.0;  // worst single frame — hitches
};

class SEXTANT_API Figure {
public:
    static std::shared_ptr<Figure> create(FigureOptions opts = {});
    ~Figure();

    // Subplot access
    std::shared_ptr<Axes> axes();
    std::shared_ptr<Axes> add_subplot(int rows, int cols, int index);

    // Display. The plot window always runs on its own background thread —
    // the calling thread never becomes the event loop.
    //
    // pause: if true, the calling thread blocks after the window appears
    //        until the user presses ENTER at the console; the window keeps
    //        rendering on its background thread the whole time and is
    //        completely unaffected once the calling thread resumes. The
    //        window closes when this Figure is destroyed (or close() is
    //        called) — keep the returned shared_ptr<Figure> alive for as
    //        long as you want the window to stay open across a pause.
    void show(bool pause = true);
    void close();
    bool is_open() const;

    // Thread-safe snapshot hand-off for live updates: copies the current
    // Axes state and publishes it to the render thread (or the blocking
    // show() loop). Throws std::logic_error if called before show(), or
    // after the window has been closed.
    void refresh();

    // File output — headless, no window required, format chosen from the
    // path's extension. Dispatches internally to whichever of the private
    // savefig_*_headless()/savefig_*() overloads below is appropriate.
    void savefig(std::string_view path);

    // Border between the figure.s edge and the subplot grid; see
    // FigureMargins in style.h.
    void set_margins(FigureMargins margins);

    // Resize the figure — the plot area, i.e. what savefig() writes and what
    // the "Plot" dock panel shows. An open window is grown by whatever its
    // menu bar and Controls panel occupy, so the plot itself lands on the
    // requested size rather than the window frame doing so. Takes effect on
    // the window's next frame; also becomes the default size for a later
    // headless savefig().
    void resize(int width, int height);

    // The figure size at which subplot `slot_index`.s plot frame (the data
    // area inside the spine) comes out frame_w x frame_h, accounting for the
    // measured insets, margins, gaps, suptitle band and that slot.s own
    // legend/colorbar. slot_index matters because two subplots of one grid
    // can have differently-sized frames. Rounding to whole pixels means the
    // achieved frame can be off by up to a pixel.
    FigureSize size_for_frame(int frame_w, int frame_h, int slot_index = 1) const;

    // size_for_frame() followed by resize().
    void resize_to_frame(int frame_w, int frame_h, int slot_index = 1);

    // fontsize is in pixels as drawn, and is stored in SuptitleOptions — so a
    // set_suptitle_style() call after this one resets it, the same way
    // set_axes_style() relates to Axes::set_title().
    void suptitle(std::string_view text, float fontsize = 21.0f);
    void set_suptitle_style(SuptitleOptions opts = {});

    // Render-loop timing (see FrameStats). All-zero before show() and after
    // close(). The counters themselves are published by the render thread,
    // but this reads the window-thread handle, so call it from the same
    // thread that drives show()/close() rather than concurrently with them.
    FrameStats frame_stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    explicit Figure(FigureOptions opts);

    // Headless. width/height <=0 default to the Figure.s own options.
    void savefig_png_headless(std::string_view path, int width = 0, int height = 0);
    void savefig_svg_headless(std::string_view path, int width = 0, int height = 0);

    // Output at the live size of the window.s "Plot" panel by default
    // (width/height <=0), spawning a window if the Figure is not open yet.
    // Not reachable from savefig(); see spec_widgets.md.
    void savefig_png(std::string_view path, int width = 0, int height = 0);
    void savefig_svg(std::string_view path, int width = 0, int height = 0);
};

} // namespace sextant
