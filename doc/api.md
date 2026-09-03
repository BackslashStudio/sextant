# sextant — API guide v0.3

All public symbols live in namespace `sextant`, behind one header:

```cpp
#include <sextant/sextant.h>
```

**Contents**

- [Two objects](#two-objects)
- [Plot types](#plot-types) — [line](#line) · [scatter](#scatter) ·
  [scatter_z](#scatter_z) · [bar](#bar) · [hist](#hist) · [heatmap](#heatmap)
- [Error bars](#error-bars)
- [Contours](#contours)
- [Titles, legends and ticks](#titles-legends-and-ticks)
- [Styling](#styling)
- [Subplots](#subplots)
- [Sizing a figure](#sizing-a-figure)
- [Saving to a file](#saving-to-a-file)
- [The interactive window](#the-interactive-window)
- [Live updates and threads](#live-updates-and-threads)
- [Errors](#errors)
- [Option reference](#option-reference)
- [Linking](#linking)

---

## Two objects

`Figure` is a window (or a file). `Axes` is one plot area inside it. That is the whole model.

```cpp
auto fig = sextant::Figure::create({.width = 900, .height = 500});
auto ax  = fig->axes();          // the figure's single default axes
ax->line(x, y).grid();
fig->show();
```

`Figure::create()` returns a `std::shared_ptr<Figure>` and is the only way to make one. **Keep that pointer alive for as long as you want the window open** — destroying the `Figure` closes it.

Every `Axes` method returns `Axes&`, so calls chain:

```cpp
fig->axes()
    ->line(x, y, {.label = "signal"})
    .scatter(px, py, {.label = "samples"})
    .set_title("Run 41")
    .legend()
    .grid();
```

Note the `->` on the first call and `.` afterwards: `axes()` hands back a smart pointer, and everything after that is a reference.

Data goes in as `std::span<const double>`, so a `std::vector`, a `std::array` or a raw pointer-and-length all work, and **nothing is retained by reference** — each call copies what it needs and your buffers are yours again immediately.

---

## Plot types

### line

```cpp
Axes& line(std::span<const double> x, std::span<const double> y, LineOptions opts = {});
Axes& line(std::span<const double> y, LineOptions opts = {});   // x = 0, 1, 2, …
```

```cpp
ax->line(t, volts, {.color = sextant::Color::Blue,
                    .linewidth = 2.0f,
                    .linestyle = sextant::LineStyle::Dashed,
                    .label = "channel A"});
```

`LineStyle::None` draws no stroke at all, and a series set to it also drops out of the legend. Dashing is honored in the window, in PNG and in SVG alike.

Lines are the type that scales: a million points pans and zooms. There is no marker option — use `scatter()` on the same data for a marked series.

### scatter

```cpp
Axes& scatter(std::span<const double> x, std::span<const double> y,
              ScatterOptions opts = {});
```

```cpp
ax->scatter(px, py, {.color = sextant::Color::Orange,
                     .size = 24.0f,
                     .marker = sextant::MarkerStyle::Diamond,
                     .alpha = 0.6f});
```

`size` is a diameter in pixels and does not change when you zoom. Markers: `Circle`, `Square`, `Triangle`, `Cross`, `Plus`, `Diamond`, `None`.

### scatter_z

A scatter whose colour carries a third value z.

```cpp
Axes& scatter_z(std::span<const double> x, std::span<const double> y,
                std::span<const double> z, ScatterZOptions opts = {});
```

```cpp
ax->scatter_z(x, y, temperature, {.vmin = -10.0f, .vmax = 40.0f,
                                  .colorbar = true});
```

Each point's colour is `z[i]` mapped through `cmap`/`vmin`/`vmax`. There is no `label`, because a colorbar rather than a legend swatch is what explains the mapping — set `colorbar = true` to get one.

### bar

```cpp
Axes& bar(std::span<const double> x, std::span<const double> height,
          BarOptions opts = {});
```

```cpp
ax->bar(months, sales, {.color = sextant::Color::Cyan,
                        .width = 0.8f,
                        .edgecolor = sextant::Color::Black});
```

`width` is a fraction of the spacing between bars, so `1.0` makes them touch. Negative heights draw downward from zero.

### hist

A histogram is a bar plot whose bars come from binning, so it takes both option structs:

```cpp
Axes& hist(std::span<const double> data, int bins = 10,
           BarOptions  bar_opts  = {.width = 1.0f},
           HistOptions hist_opts = {});
```

```cpp
ax->hist(samples, 40, {.color = sextant::Color::Green},
                      {.density = true});
```

`BarOptions` says how the bars are *drawn*; `HistOptions` says what binning means (`density`, `cumulative`). Here `width` is read against the **bin** width.

**One wart worth knowing.** `hist()`'s default argument raises `width` to `1.0` so bins touch. If you pass your own `BarOptions` you get `BarOptions`' own default of `0.8` back — and gapped bins read as a bar chart. Set `.width = 1.0f` yourself whenever you pass bar options to `hist()`.

`hist()` is also the one place `BarOptions::errorbar` is ignored: a bin height is a count sextant derived, not something you measured.

### heatmap

```cpp
Axes& heatmap(std::span<const float> data, int rows, int cols,
              HeatmapOptions opts = {});
```

```cpp
ax->heatmap(field, rows, cols, {.vmin = 0.0f, .vmax = 1.0f,
                                .colorbar = true});
```

`data` is **row-major**, `rows * cols` floats, indexed `row * cols + col`. The image occupies `[0, cols] x [0, rows]` in data space, so a cell's centre sits at `(col + 0.5, row + 0.5)`.

`origin` decides which end row 0 draws at — `"lower"` (default) puts it at the bottom, `"upper"` at the top. The buffer layout does not change either way.

---

## Error bars

An error bar decorates a series you already drew, so it lives in that series' options rather than in a call of its own. It is two shapes, either omittable:

- a **capped whisker** from `ymin[i]` to `ymax[i]` — **absolute data coordinates**, not offsets, so a range need not be centred on the point. Leave one end empty for a one-sided whisker.
- a **box** spanning `y[i] ± yvar[i]`, drawn exactly as given (not square-rooted), `boxwidth` pixels across.

```cpp
ax->line(x, y, {.color = sextant::Color::Blue, .label = "measured",
                .errorbar = {.ymin = low, .ymax = high, .yvar = sigma}});

// Either half alone is fine — a box with no whisker, styled:
ax->line(x, y, {.errorbar = {.yvar = sigma,
                             .color = sextant::Color::Gray,
                             .boxwidth = 26.0f, .box_alpha = 0.55f}});

// Bars hang theirs off the bar's tip, not the baseline:
ax->bar(centers, heights, {.errorbar = {.ymin = blo, .ymax = bhi}});

// Only the scatter kinds take x, and their box is a real 2D rectangle:
ax->scatter(x, y, {.errorbar = {.ymin = ylo, .ymax = yhi, .yvar = sy,
                                .xmin = xlo, .xmax = xhi, .xvar = sx}});
```

**Direction support differs by plot type, and it is enforced rather than ignored:**

| | y whisker / box | x whisker / box |
|---|---|---|
| `line`, `bar` | yes | **throws** |
| `scatter`, `scatter_z` | yes | yes, with a 2D box |
| `hist` | dropped silently | dropped silently |

Every non-empty vector must hold exactly one entry per point, or the call throws. Axis limits widen to fit the bars, so nothing gets clipped away.

---

## Contours

A heatmap can trace iso-lines through itself.

```cpp
ax->heatmap(field, rows, cols, {
        .vmin = 0.0f, .vmax = 2.0f, .colorbar = true,
        .contours = {0.4, 0.8, 1.2, 1.6},
        .contour_color = {1.0f, 1.0f, 1.0f, 0.9f},
        .contour_linewidth = 1.5f,
        .contour_labels = true});
```

Levels are **z values in your data's own units** — the numbers the colorbar shows, not a 0..1 scale. An empty list draws nothing and computes nothing. Levels are sorted and de-duplicated for you; a non-finite one throws.

`contour_labels` writes each level onto its own line, rotated to follow it, with the line broken to make room. A line too short to break keeps the line and drops the label.

Lines pass through **cell centres**, so a contour stops half a cell inside the image, and a heatmap smaller than 2×2 traces nothing. `origin` is honored.

---

## Titles, legends and ticks

```cpp
ax->set_title("Run 41", 18.0f)
   .set_xtitle("time (s)")
   .set_ytitle("voltage (V)")
   .set_xlim(0.0, 10.0)
   .set_ylim(-1.0, 1.0)
   .grid()
   .legend();

fig->suptitle("Experiment 7");     // one title over the whole subplot grid
```

"Title" names an axis or the whole axes; "label" is the text under an individual tick. `fontsize` is in pixels as drawn.

Limits are automatic until you set them. `set_xlim`/`set_ylim` pin them; error bars and contours are included when they are automatic.

Only series with a non-empty `label` appear in the legend, and it renders *beside* the plot rather than over your data.

Explicit ticks:

```cpp
std::vector<double> days{0, 1, 2, 3, 4};
ax->set_xticks(days, {"Mon", "Tue", "Wed", "Thu", "Fri"});
ax->set_xticks(days);                // positions only, values as labels
```

The positions have to be a named array or vector, not a braced list written in place: they arrive as `std::span<const double>`, and `std::span` gains a constructor from `std::initializer_list` only in C++26. The labels are a `std::vector<std::string>`, so those *can* be written inline.

`cla()` clears every plot object and resets the limits.

> **Ordering gotcha.** `set_title(text, size)` stores its size in the axes
> style, so a later `set_axes_style()` resets it. Call `set_axes_style()` first.

---

## Styling

Colours are plain `{r, g, b, a}` floats in 0..1, with named constants and two parsers:

```cpp
sextant::Color::Blue;                        // also Red Green Orange Purple
                                             // Cyan Black White Gray
sextant::Color::from_hex(0x1f77b4);
sextant::Color::from_name("orange");         // throws on an unknown name
sextant::Color{0.2f, 0.4f, 0.9f, 0.5f};      // half-transparent blue
```

The named constants are matplotlib's tab10 palette, so `Color::Blue` is `rgb(31,119,180)` rather than pure blue.

Four structs cover the rest, each applied through its own setter:

```cpp
ax->set_axes_style({.spine_color = sextant::Color::Gray,
                    .label_fontsize = 13.0f,
                    .title_fontsize = 22.0f,
                    .font_path = "C:/Windows/Fonts/times.ttf"});

ax->grid(true, {.color = {0.85f, 0.85f, 0.85f, 1.0f},
                .linestyle = sextant::LineStyle::Dotted});

ax->legend({.offset_x = 14.0f, .fontsize = 12.0f, .frameon = true});

ax->set_colorbar_style({.fontsize = 12.0f});
fig->set_suptitle_style({.fontsize = 26.0f, .align = sextant::HAlign::Left});
```

`font_path` is an absolute path to a `.ttf`/`.ttc`/`.otf`. Leave it empty for the default, which sextant discovers from your system font directories. The legend, colorbar and suptitle each have their own `font_path` and do **not** inherit the axes one.

`set_colorbar_style()` only styles a colorbar; one appears because a plot object asked for it via `HeatmapOptions::colorbar` or `ScatterZOptions::colorbar`. One colorbar is drawn per axes.

---

## Subplots

```cpp
auto fig = sextant::Figure::create({.width = 1400, .height = 800,
                                    .subplot_col_gap = 20.0f,
                                    .subplot_row_gap = 20.0f});

fig->add_subplot(2, 3, 1)->line(x, a).set_title("A");
fig->add_subplot(2, 3, 2)->line(x, b).set_title("B");
// … index runs 1..rows*cols, row-major, like matplotlib
```

The gaps separate *whole subplots*, labels and titles included. The space each decoration needs is measured from its own text, so changing a font size moves the text and the room made for it together — there is nothing to tune.

`FigureMargins` is the border between the figure edge and the grid:

```cpp
fig->set_margins({.left = 20.0f, .right = 20.0f,
                  .top = 20.0f, .bottom = 20.0f});
```

Mixing `axes()` and `add_subplot()` on one figure is not meaningful — pick one.

---

## Sizing a figure

`FigureOptions::width`/`height` and `resize()` describe the **plot area** — what `savefig()` writes. An open window grows by whatever its menu bar and control panel occupy, so the plot lands on the size you asked for.

You can also work backwards from the data area you want:

```cpp
sextant::FigureSize s = fig->size_for_frame(400, 300);   // figure size for a
                                                         // 400x300 plot frame
fig->resize_to_frame(400, 300);                          // …and apply it
fig->resize_to_frame(400, 300, /*slot_index=*/2);        // of subplot 2
```

`slot_index` matters because a legend or colorbar is carved out of the cell that owns it, so two subplots of one grid can have different frame sizes. The result is exact apart from rounding to whole pixels.

---

## Saving to a file

```cpp
fig->savefig("plot.png");
fig->savefig("plot.svg");
```

The format comes from the extension; anything else throws. **No window is required for either** — both work headless, and SVG needs no OpenGL context at all, so it runs on a server with no display.

- **PNG** is supersampled and box-filtered, so it matches the figure size exactly. Control it with `FigureOptions::supersample` (default 2, max 4, 1 to disable). Cost is quadratic: 2 means four times the fragments.
- **SVG** is vector and resolution-independent, so `supersample` does not apply. Text names a font family for the viewer to resolve, matching whatever the window draw.

If the figure window is already opened and resized, `savefig()` will still save the figure with width/height set in `Figure::create`, not the window size, it reuses that window's GL context rather than standing up its own, which is most of the cost of a save.

---

## The interactive window

`fig->show()` opens a window with a Dear ImGui control panel. It always runs on its own thread.

**Menus**

| | |
|---|---|
| **File → Save** | Write PNG/SVG at a chosen size, or at a chosen *plot-frame* size |
| **File → Resize to plot frame** | Resize the live window so the plot area hits a target |
| **View → Control Panel / Data Panel** | Show or hide either side panel |
| **Edit → Navigate** | Left-drag pans, scroll zooms at the cursor, double-click resets |
| **Edit → Hints** | Hover a point for its values (on by default) |

**Navigate** always acts on the axes selected in the Controls panel's *Axis* combo, whichever subplot the cursor is over. **Hints** work over any subplot regardless of that selection, and read out `x`, `y`, plus `z` for `scatter_z`, row/column/value for a heatmap, and `±var [min, max]` for a series with error bars.

**Controls panel** — edit titles, limits, tick positions and labels, grid, legend, colorbar, the suptitle, margins and gaps, and every colour and font size, live.

**Data panel** — the current axes' plot objects as editable tables: x/y/z columns side by side, heatmap matrices as a 2D grid. You can retype any value, insert and delete points, insert and delete matrix rows and columns, and choose the numeric notation and precision. Cells are tinted by where their value falls in their column's range; the *Shade cells* checkbox turns that off.

Give each point custom hover text with `hint_labels`, index-aligned with your data:

```cpp
ax->line(x, y, {.hint_labels = {"", "", "Peak", "", "Trough"}});
```

The panel's own look is set once at creation:

```cpp
sextant::Figure::create({.theme = sextant::PanelTheme::Dark,   // or Light,
                         .panel_width = 300.0f});              // Classic
```

---

## Live updates and threads

The window renders on its own thread from a snapshot of your data. You can keep mutating `Axes` from your thread, then publish:

```cpp
fig->show(false);                 // don't block
while (running) {
    simulate(step);
    fig->axes()->cla();
    fig->axes()->line(t, y);
    fig->refresh();               // publish a new snapshot
    if (!fig->is_open()) break;   // user closed the window
}
```

**The rules:**

- `show(true)` — the default — blocks your thread until you press ENTER at the console. The window keeps rendering the whole time and is unaffected when you resume.
- `show(false)` returns immediately.
- **Call `axes()`, `add_subplot()`, `refresh()`, `savefig()`, `resize()` and `close()` from one thread — the one that owns the `Figure`.** Only `is_open()` is safe to call from anywhere.
- `refresh()` throws `std::logic_error` before `show()` or after the window has closed.
- Edits you make in the Data panel survive a `refresh()`. Panel edits to titles and limits are live preview and do not — a `refresh()` republishes what your code says.

`frame_stats()` reports render timing, cumulative since `show()`:

```cpp
auto a = fig->frame_stats();
/* … */
auto b = fig->frame_stats();
double ms_per_frame = (b.total_ms - a.total_ms) / (b.frames - a.frames);
```

It measures render work and excludes the vsync-blocking buffer swap, so `b.frames` over wall time is the rate actually achieved while `total_ms` is what scales with your data size. Set `FigureOptions::vsync = false` to measure end-to-end cost.

---

## Errors

sextant throws standard exceptions; it never aborts and never writes to stderr behind your back.

| Thrown | When |
|---|---|
| `std::invalid_argument` | mismatched `x`/`y` lengths; an error-bar vector that is not one entry per point; an x error bar on `line()`/`bar()`; non-positive heatmap `rows`/`cols`; heatmap data too small; a non-finite contour level; a bad `add_subplot` index; a non-positive size; an unknown `savefig` extension; a bad colour name or hex |
| `std::logic_error` | `refresh()` before `show()`, or after the window closed |

Anything that would silently draw a misleading plot is an exception rather than a best guess.

---

## Option reference

```cpp
struct LineOptions {
    Color       color     = Color::Blue;
    float       linewidth = 1.5f;
    LineStyle   linestyle = LineStyle::Solid;   // Solid Dashed Dotted DashDot None
    std::string label;                          // non-empty => legend entry
    float       alpha     = 1.0f;
    ErrorBarOptions          errorbar;          // y direction only
    std::vector<std::string> hint_labels;
};

struct ScatterOptions {
    Color       color  = Color::Blue;
    float       size   = 20.0f;                 // pixel diameter
    MarkerStyle marker = MarkerStyle::Circle;
    std::string label;
    float       alpha  = 0.8f;
    ErrorBarOptions          errorbar;          // both directions
    std::vector<std::string> hint_labels;
};

struct ScatterZOptions {
    Colormap    cmap     = Colormap::Viridis;
    float       size     = 20.0f;
    MarkerStyle marker   = MarkerStyle::Circle;
    float       alpha    = 0.8f;
    float       vmin     = 0.0f;
    float       vmax     = 1.0f;
    bool        colorbar = false;
    ErrorBarOptions          errorbar;          // both directions
    std::vector<std::string> hint_labels;
};

struct BarOptions {
    Color       color     = Color::Blue;
    float       width     = 0.8f;               // fraction of the spacing
    float       alpha     = 1.0f;
    std::string label;
    Color       edgecolor = Color::Black;
    float       linewidth = 0.5f;
    ErrorBarOptions          errorbar;          // y only; ignored by hist()
    std::vector<std::string> hint_labels;
};

struct HistOptions {
    bool density    = false;
    bool cumulative = false;
};

struct HeatmapOptions {
    Colormap    cmap     = Colormap::Viridis;
    float       vmin     = 0.0f;
    float       vmax     = 1.0f;
    bool        colorbar = false;
    std::string origin   = "lower";             // or "upper"

    std::vector<double> contours;               // z levels; empty = none
    Color contour_color     = Color::Black;
    float contour_linewidth = 1.0f;
    bool  contour_labels    = false;
    float contour_fontsize  = 10.0f;

    std::vector<std::string> hint_labels;       // row-major, rows*cols
};

struct ErrorBarOptions {
    std::vector<double> ymin, ymax;   // whisker span, absolute data coords
    std::vector<double> yvar;         // box half-height, data units
    std::vector<double> xmin, xmax;   // scatter kinds only
    std::vector<double> xvar;
    std::optional<Color> color;       // unset = the series' own colour
    float linewidth = 1.0f;
    float capsize   = 6.0f;           // total cap length, pixels; 0 = no caps
    float boxwidth  = 10.0f;          // total box width, pixels
    float box_alpha = 0.25f;          // fill opacity; 0 = outline only
};

struct GridOptions  { Color color; LineStyle linestyle = LineStyle::Solid;
                      float linewidth = 0.5f; };

struct AxesStyle    { Color spine_color; float spine_linewidth = 1.0f;
                      Color tick_color;  float tick_length = 5.0f,
                                               tick_linewidth = 1.0f;
                      Color label_color; float label_fontsize  = 11.0f;
                      Color title_color; float title_fontsize  = 18.0f;
                      Color xtitle_color; float xtitle_fontsize = 16.5f;
                      Color ytitle_color; float ytitle_fontsize = 16.5f;
                      std::string font_path; };

struct LegendOptions { float offset_x = 10.0f, offset_y = 0.0f;
                       float fontsize = 10.0f; bool frameon = true;
                       Color text_color, frame_color, border_color;
                       float border_linewidth = 1.0f;
                       std::string font_path; };

struct ColorbarOptions { float fontsize = 10.0f;
                         Color text_color, border_color;
                         float border_linewidth = 1.0f;
                         std::string font_path; };

struct SuptitleOptions { float fontsize = 21.0f; Color color;
                         std::string font_path;
                         HAlign align = HAlign::Center;   // Left Center Right
                         float offset_x = 0.0f, offset_y = 0.0f; };

struct FigureMargins { float left = 10.0f, right = 10.0f,
                             top  = 10.0f, bottom = 10.0f; };

struct FigureOptions {
    int         width = 800, height = 600;
    std::string title = "sextant";       // window title bar
    bool        resizable = true;
    float       dpi = 96.0f;             // currently unused
    float       subplot_col_gap = 0.0f, subplot_row_gap = 0.0f;
    FigureMargins margins;
    float       panel_width = 240.0f;
    int         supersample = 2;         // 1..kMaxSupersample (4)
    bool        vsync = true;
    PanelTheme  theme = PanelTheme::Light;   // Dark Light Classic
};

struct FrameStats { unsigned long long frames; double total_ms, last_ms, max_ms; };
```

`Colormap` currently offers `Viridis` only.

---

## Linking

```cmake
list(APPEND CMAKE_PREFIX_PATH "/path/to/sextant/dist")   # where you installed it
find_package(sextant CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE sextant::sextant)
```

See the README for installing and for the Windows DLL-copy step.

`sextant::sextant` is the shared library, and it is self-contained in both directions: FreeType, libpng, zlib and GLFW are linked into it, so `sextant.dll` depends on nothing but OS DLLs and the MSVC runtime at runtime, and **you do not need any of them installed to build against it** either.

`sextant::sextant_static` links statically instead. A static library cannot embed its dependencies, so your project resolves FreeType, libpng and GLFW itself — and **if you use vcpkg you must pin the same triplet before your `project()` call:**

```cmake
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
project(my_app CXX)
```

vcpkg resolves `find_package` once, at the first `project()`, so a consumer that does not pin it silently gets the dynamic triplet's import libraries — which links, and defeats the point. The shared library has no such requirement.
