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

// Color preset for the Cosmetic/Plot panel chrome only -- not for plot
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
    // live-editable from the Cosmetic panel's Layout section.
    FigureMargins margins;

    // Initial width in pixels of the Cosmetic panel docked to the right edge
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

// A subplot covering a rectangle of grid cells, from its top-left cell to its
// bottom-right one (1-based, row-major). Written braced at the call site --
// add_subplot(2, 3, {4, 6}), add_subplot({4, 6}) -- as matplotlib writes
// subplot(2, 3, (4, 6)); a plain pair of ints would read as a grid shape.
struct SubplotSpan {
    int first = 1;
    int last  = 1;
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

// ---------------------------------------------------------------------------
// Export options — the two places an exporter is allowed to give up
// ---------------------------------------------------------------------------
// A 3D scene whose objects *interpenetrate* has no correct order at the
// granularity of whole objects, so both output paths resolve it per pixel or
// per piece — and both are bounded, because the alternative to a bound is an
// export that never returns. When a bound binds, the picture is still drawn
// and part of it is in plain depth order, which is to say on the wrong side of
// something. These options are how a caller who has such a scene raises the
// bound, and `SvgSaveReport` is how they find out one was hit.
//
// Nothing here matters for a scene without interpenetrating 3D geometry, which
// is nearly every scene: the defaults are sized so that everything the gallery
// draws finishes exactly.

// SVG. The vector path has no depth buffer, so it orders the scene with
// Newell's algorithm and *splits* a pair of polygons when the pairwise tests
// cannot separate them (see spec_3d.md §10). Splitting is what makes the
// relation an order at all, and a sheet threaded through a grid of translucent
// bars can need tens of thousands of cuts.
struct SvgExportOptions {
    // Maximum splits. 0 selects the automatic bound, `8 * polygons + 64`,
    // which is generous for a scene where one object cuts another and not
    // generous for one where two objects interleave everywhere. This is the
    // bound that binds in practice; `SvgSaveReport::splits` names the number
    // it stopped at, so it can be raised against a real figure rather than
    // guessed at.
    std::size_t max_splits = 0;

    // Maximum pairwise comparisons. 0 selects 20,000,000. Raise this only if
    // a report says it was what ran out — it is the wall-clock bound, and a
    // scene that reaches it is one where the export takes minutes.
    std::size_t max_tests = 0;
};

// What one SVG export did about ordering. `scene_order_exact` false means part
// of the file is knowingly wrong, and `warning` says so in a sentence — the
// same sentence written into the SVG as an XML comment and printed once to
// stderr, so a caller who ignores this struct is not left with a silently
// wrong picture.
struct SvgSaveReport {
    bool        scene_order_exact = true;
    std::size_t splits = 0;
    std::size_t tests  = 0;
    std::string warning;
};

// PNG. The raster path peels translucent geometry into layers, front to back,
// and composites them in the order the pixel's own ray meets them; the loop
// stops early once a pass peels nothing, so the cap only binds where a ray
// really does cross more translucent surfaces than that.
struct PngExportOptions {
    // Depth-peeling layers, 1..64. 0 selects the default of 8. A translucent
    // bar contributes two layers on its own, so a sheet inside a grid of them
    // can exceed 8 along some rays; raising this costs one full geometry pass
    // per added layer and nothing at all where the early-out already fires.
    int peel_layers = 0;
};

class SEXTANT_API Figure {
public:
    static std::shared_ptr<Figure> create(FigureOptions opts = {});
    ~Figure();

    // Subplot access. A Figure has one grid: the first call that gives
    // rows/cols (or axes(), which is 1x1) fixes its shape, and a later call
    // giving a different shape throws. The shape-less overloads use that
    // shape and throw if none is fixed yet.
    //
    // A subplot is one cell, `index`, or a rectangle of cells, `{first,
    // last}` -- its top-left and bottom-right cells. Cells are 1-based and
    // row-major, as in matplotlib, so on a 2x3 grid {4, 6} is the whole
    // bottom row and {1, 4} the left column. A span is addressed afterwards
    // by its first cell (size_for_frame(), the panels' selector).
    //
    // Each call returns the subplot already at exactly that cell or span, or
    // creates one there. Any other request touching an occupied cell throws,
    // including a single index inside an existing span: it names a cell,
    // not the subplot covering it.
    std::shared_ptr<Axes> axes();
    std::shared_ptr<Axes> add_subplot(int rows, int cols, int index);
    std::shared_ptr<Axes> add_subplot(int rows, int cols, SubplotSpan span);
    std::shared_ptr<Axes> add_subplot(int index);
    std::shared_ptr<Axes> add_subplot(SubplotSpan span);

    // A 3D axes on the same grid, under the same rules. 2D and 3D subplots
    // coexist freely: the grid rules are about the cells, not about what
    // occupies them. Requesting a cell or span that already holds the other
    // kind throws rather than replacing what is in it.
    std::shared_ptr<Axes3D> add_subplot3d(int rows, int cols, int index);
    std::shared_ptr<Axes3D> add_subplot3d(int rows, int cols, SubplotSpan span);
    std::shared_ptr<Axes3D> add_subplot3d(int index);
    std::shared_ptr<Axes3D> add_subplot3d(SubplotSpan span);

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
    // path's extension. Dispatches to savefig_png()/savefig_svg() below with
    // that format's default options.
    //
    // It returns nothing even though an SVG export can report something, since
    // what it would report depends on which format the path named. A caller
    // who wants the report calls savefig_svg() directly; a caller who does not
    // still gets the warning on stderr.
    void savefig(std::string_view path);

    // The same output, per format, so each can take the options only it has.
    // Headless exactly as savefig() is — no window is opened and none is
    // needed. width/height <= 0 use the Figure's own size.
    SvgSaveReport savefig_svg(std::string_view path, SvgExportOptions opts = {},
                              int width = 0, int height = 0);
    void          savefig_png(std::string_view path, PngExportOptions opts = {},
                              int width = 0, int height = 0);

    // Border between the figure.s edge and the subplot grid; see
    // FigureMargins in style.h.
    void set_margins(FigureMargins margins);

    // How the subplot grid shares out its width among columns and its height
    // among rows: one weight per column (per row), so {2, 1} makes the left
    // column twice as wide as the right. A weight sizes the whole cell,
    // decorations included; a span gets the sum of the weights it covers.
    // Empty restores equal weights, which is the default.
    //
    // Throws std::invalid_argument for a weight that is not finite and
    // positive, or for a count that does not match the grid -- checked here
    // once a subplot has fixed the grid shape, and otherwise by the call that
    // fixes it. Dragging a boundary between two subplots in the window
    // changes these too, and a dragged value is kept by the next refresh();
    // a later call here replaces it.
    void set_col_ratios(std::vector<float> ratios);
    void set_row_ratios(std::vector<float> ratios);

    // The weights as last set by the calls above or folded in by refresh();
    // empty when they are equal by default.
    std::vector<float> col_ratios() const;
    std::vector<float> row_ratios() const;

    // Resize the figure — the plot area, i.e. what savefig() writes and what
    // the "Plot" dock panel shows. An open window is grown by whatever its
    // menu bar and Cosmetic panel occupy, so the plot itself lands on the
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

    // Output at the live size of the window.s "Plot" panel by default
    // (width/height <=0), spawning a window if the Figure is not open yet.
    // Not reachable from savefig(); see spec_widgets.md.
    void          savefig_png_live(std::string_view path, PngExportOptions opts = {},
                                   int width = 0, int height = 0);
    SvgSaveReport savefig_svg_live(std::string_view path, SvgExportOptions opts = {},
                                   int width = 0, int height = 0);
};

} // namespace sextant
