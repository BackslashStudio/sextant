# Option reference

Every option struct, enum and data struct in the public API, with its defaults.
Write option structs with designated initializers, naming only the fields you
set, in the order they appear here. Sizes are logical pixels (1/96 inch) unless
a row says otherwise; colours are `Color`.

For how these are used, see the [API guide](api.md) and [3D plots](3d.md).

- [Figure and output](#figure-and-output): [FigureOptions](#figureoptions) ·
  [FigureMargins](#figuremargins) · [SuptitleOptions](#suptitleoptions) ·
  [SubplotSpan](#subplotspan) · [FigureSize](#figuresize) ·
  [PngExportOptions](#pngexportoptions) · [SvgExportOptions](#svgexportoptions) ·
  [SvgSaveReport](#svgsavereport) · [SvgRender](#svgrender) · [RgbaImage](#rgbaimage) ·
  [FrameStats](#framestats)
- [2D plot options](#2d-plot-options): [LineOptions](#lineoptions) ·
  [ScatterOptions](#scatteroptions) · [ScatterZOptions](#scatterzoptions) ·
  [BarOptions](#baroptions) · [HistOptions](#histoptions) ·
  [HeatmapOptions](#heatmapoptions) · [FillOptions](#filloptions) ·
  [StackOptions](#stackoptions) · [QuiverOptions](#quiveroptions) ·
  [AxLineOptions](#axlineoptions) · [AxSpanOptions](#axspanoptions) ·
  [Range](#range) · [ErrorBar](#errorbar) · [ErrorBarOptions](#errorbaroptions)
- [Decoration and style](#decoration-and-style): [Color](#color) · [Colormap](#colormap) ·
  [AxesStyle](#axesstyle) · [ScaleOptions](#scaleoptions) · [GridOptions](#gridoptions) ·
  [LegendOptions](#legendoptions) · [ColorbarOptions](#colorbaroptions)
- [Text](#text): [Pos](#pos) · [TextOptions](#textoptions) · [ArrowOptions](#arrowoptions)
- [Events](#events): [Event](#event)
- [3D options](#3d-options): [Camera3D](#camera3d) · [Vec3](#vec3) ·
  [BoxAspect](#boxaspect) · [Box3DStyle](#box3dstyle) ·
  [Plane2DOptions](#plane2doptions) · [Bar3DOptions](#bar3doptions) ·
  [SurfaceOptions](#surfaceoptions) · [SurfaceTriOptions](#surfacetrioptions) ·
  [Scatter3DOptions](#scatter3doptions) · [Line3DOptions](#line3doptions) ·
  [ErrorBar3D](#errorbar3d) · [ErrorBar3DOptions](#errorbar3doptions)
- [Enums](#enums)
- [Data structs](#data-structs)

---

## Figure and output

### FigureOptions

Passed to `Figure::create()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `width`, `height` | `int` | `800`, `600` | Size of the plot area, which is what `savefig()` writes. A window grows by its menu bar and panels |
| `title` | `std::string` | `"sextant"` | Window title bar |
| `resizable` | `bool` | `true` | Whether the window can be resized |
| `dpi` | `float` | `96` | PNG resolution: `dpi / 96` output pixels per logical pixel. Finite and positive |
| `subplot_col_gap`, `subplot_row_gap` | `float` | `0` | Space between subplot cells (a cell includes its decorations) |
| `margins` | `FigureMargins` | 10 each | Figure edge to subplot grid; also `set_margins()` |
| `background` | `Color` | `{0.93, 0.93, 0.93, 1}` | Fill behind everything (margins, gaps, suptitle); alpha 0 is transparent in PNG and SVG. Also `set_background()` |
| `mathtext` | `bool` | `true` | Draw `$...$` spans in every string as math; `false` draws every string as written |
| `math_fontset` | `MathFontset` | `Text` | The font math draws in, from the fonts installed: `Text` the text's own, `Stix` (STIX Two Math, STIX Two Text, STIXGeneral), `Cm` (Latin Modern Math, CMU Serif, Latin Modern Roman), `Custom` the file at `math_font_path`. None installed: the text font, and one message. Fixed at `create()` |
| `math_font_path` | `std::string` | empty | The font file for `MathFontset::Custom` |
| `pick_radius` | `float` | `12` | How near, in pixels, the cursor must come to a point, marker, arrow or path vertex for hover and picks; bars, fills and heatmaps answer inside them. Finite and >= 0; also `set_pick_radius()` |
| `panel_width` | `float` | `240` | Initial width of the window's side panels. Never exported |
| `supersample` | `int` | `2` | Supersampling for window and PNG, 1..4; 1 disables. Cost is quadratic. No effect on SVG |
| `vsync` | `bool` | `true` | Cap the render loop at the display refresh rate. Turn off only to measure |
| `theme` | `PanelTheme` | `Light` | Look of the window's panels (not the plot) |

### FigureMargins

| Field | Type | Default | Meaning |
|---|---|---|---|
| `left`, `right`, `top`, `bottom` | `float` | `10` each | Space between the figure edge and the subplot grid |

### SuptitleOptions

Passed to `Figure::set_suptitle_style()`. The text is `Figure::suptitle()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `fontsize` | `float` | `21` | Also set by `suptitle(text, fontsize)` |
| `color` | `Color` | `{0.1, 0.1, 0.1, 1}` | |
| `font_path` | `std::string` | empty | Absolute font file path; empty = default font |
| `align` | `HAlign` | `Center` | `Left`/`Right` anchor to the figure edge |
| `offset_x`, `offset_y` | `float` | `0` | Nudge as drawn; does not enlarge the title band |

### SubplotSpan

`add_subplot(rows, cols, {first, last})`. Write it braced.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `first` | `int` | `1` | Top-left cell, 1-based, row-major |
| `last` | `int` | `1` | Bottom-right cell |

### FigureSize

Returned by `size_for_frame()`.

| Field | Type | Meaning |
|---|---|---|
| `width`, `height` | `int` | Figure size; `{0, 0}` when there are no axes to size against |

### PngExportOptions

Passed to `Figure::savefig_png()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `peel_layers` | `int` | `0` (= 8) | Depth-peeling layers for translucent 3D, 1..64 |
| `dpi` | `float` | `0` (= `FigureOptions::dpi`) | Resolution of this file; same layout at any dpi |

### SvgExportOptions

Passed to `Figure::savefig_svg()`. Only matters for 3D scenes with interpenetrating translucent geometry.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `max_splits` | `std::size_t` | `0` (= `8 × polygons + 64`) | Polygon splits allowed; the bound that binds in practice |
| `max_tests` | `std::size_t` | `0` (= 20,000,000) | Pairwise comparisons allowed; the wall-clock bound |

### SvgSaveReport

Returned by `Figure::savefig_svg()`.

| Field | Type | Meaning |
|---|---|---|
| `scene_order_exact` | `bool` | `false` if a bound was hit and part of the file is in plain depth order |
| `splits`, `tests` | `std::size_t` | Work done |
| `warning` | `std::string` | Which bound was hit and the number to beat; empty when exact |

### SvgRender

Returned by `Figure::render_svg()`.

| Field | Type | Meaning |
|---|---|---|
| `svg` | `std::string` | The document `savefig_svg()` would write |
| `report` | `SvgSaveReport` | As `savefig_svg()` returns |

### RgbaImage

Returned by `Figure::render_rgba()`: the pixels a PNG export encodes.

| Field | Type | Meaning |
|---|---|---|
| `width`, `height` | `int` | Output pixels (follow `PngExportOptions::dpi`) |
| `pixels` | `std::vector<std::uint8_t>` | 8-bit RGBA, top row first, no padding: `width * height * 4` bytes |

### FrameStats

Returned by `Figure::frame_stats()`; cumulative since `show()`.

| Field | Type | Meaning |
|---|---|---|
| `frames` | `unsigned long long` | Frames rendered |
| `total_ms` | `double` | Summed render work, excluding the vsync-blocking swap |
| `last_ms` | `double` | The most recent frame |
| `max_ms` | `double` | The worst single frame |

---

## 2D plot options

### LineOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Unset = the axes' next [colour-cycle](api.md#the-model) colour, taken at the call |
| `linewidth` | `float` | `1.5` | |
| `linestyle` | `LineStyle` | `Solid` | `None` draws no stroke and drops the series from the legend |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | Keyed when this is true and `name` is not empty |
| `visible` | `bool` | `true` | `false` hides the object as if absent: not drawn or exported, no legend key, colorbar, hover or pick, and out of the automatic limits. Its data stays. Also `set_line_visible()` and the like |
| `alpha` | `float` | `1` | |
| `loop` | `bool` | `false` | Add a closing segment from the last point to the first |
| `drawstyle` | `DrawStyle` | `Default` | `StepsPre`, `StepsMid`, `StepsPost` draw the line as steps |
| `errorbar` | `ErrorBarOptions` | | Style of the error bars passed as an `ErrorBar` |
| `hint_labels` | `std::vector<std::string>` | empty | Extra hover text per point |

### ScatterOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | As `LineOptions::color` |
| `size` | `float` | `20` | Marker diameter; constant under zoom |
| `sizes` | `std::vector<double>` | empty | One diameter per point (a bubble chart); empty = `size`. A NaN or non-positive size hides the point. Kept as data: `ScatterData::sizes` |
| `marker` | `MarkerStyle` | `Circle` | |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `alpha` | `float` | `0.8` | Fill opacity, on top of `color`'s own; 0 = a hollow marker |
| `edgecolor` | `std::optional<Color>` | unset | Outline colour; unset = the fill colour |
| `edge_alpha` | `float` | `1` | Outline opacity, independent of `alpha` |
| `edge_linewidth` | `float` | `0` | Outline width, drawn inside the marker; 0 = none. The stroke width of the line markers |
| `errorbar` | `ErrorBarOptions` | | |
| `hint_labels` | `std::vector<std::string>` | empty | |

### ScatterZOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `cmap` | `Colormap` | `Viridis` | |
| `size` | `float` | `20` | |
| `sizes` | `std::vector<double>` | empty | As `ScatterOptions::sizes` |
| `marker` | `MarkerStyle` | `Circle` | |
| `alpha` | `float` | `0.8` | |
| `edgecolor`, `edge_alpha`, `edge_linewidth` | | unset, `1`, `0` | As in `ScatterOptions`; unset colour = each point's own |
| `vmin`, `vmax` | `float` | `0`, `1` | Data range the colormap spans |
| `colorbar` | `bool` | `false` | Draw a colorbar for this series |
| `name` | `std::string` | empty | Titles the colorbar; also the legend key (marker filled white, black edge) |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `errorbar` | `ErrorBarOptions` | | Unset colour = black |
| `hint_labels` | `std::vector<std::string>` | empty | Default hover text is `x=.., y=.., z=..` |

### BarOptions

Also the bar style of `hist()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | As `LineOptions::color` |
| `orientation` | `Orientation` | `Vertical` | `Horizontal` lays the bars along x (matplotlib's `barh`) |
| `bottom` | `std::vector<double>` | empty | Where each bar starts: one value, or one per bar (stacking, waterfalls). Kept as data: `BarData::bottom` |
| `width` | `float` | `0.8` | Fraction of the bar spacing (for `hist()`, of the bin width) |
| `alpha` | `float` | `1` | |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `edgecolor` | `Color` | `Black` | |
| `linewidth` | `float` | `0.5` | Edge width |
| `errorbar` | `ErrorBarOptions` | | Hung off the bar's end; unset colour = `edgecolor` |
| `hint_labels` | `std::vector<std::string>` | empty | |

### HistOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `density` | `bool` | `false` | Normalize so the bars' area is 1 |
| `cumulative` | `bool` | `false` | Each bin counts itself and every bin before it |

### HeatmapOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `cmap` | `Colormap` | `Viridis` | |
| `vmin`, `vmax` | `float` | `0`, `1` | Data range the colormap spans |
| `colorbar` | `bool` | `false` | |
| `name` | `std::string` | empty | Titles the colorbar |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `origin` | `std::string` | `"lower"` | `"lower"` draws row 0 at the bottom, `"upper"` at the top; anything else throws |
| `contours` | `std::vector<double>` | empty | Contour levels in data units |
| `contour_color` | `Color` | `Black` | |
| `contour_linewidth` | `float` | `1` | |
| `contour_labels` | `bool` | `false` | Write each level on its line |
| `contour_fontsize` | `float` | `10` | |
| `hint_labels` | `std::vector<std::string>` | empty | Row-major, `row * cols + col` |

### FillOptions

Passed to `fill()`; `stack()` builds one per layer.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `orientation` | `Orientation` | `Horizontal` | `Horizontal` fills between y1(x) and y2(x) (`fill_between`); `Vertical` between x1(y) and x2(y) (`fill_betweenx`) |
| `color` | `std::optional<Color>` | unset | As `LineOptions::color` |
| `alpha` | `float` | `1` | |
| `edgecolor` | `std::optional<Color>` | unset | Outline of each region; unset = `color` |
| `linewidth` | `float` | `0` | Outline width; 0 = none |
| `linestyle` | `LineStyle` | `Solid` | Of the outline |
| `drawstyle` | `DrawStyle` | `Default` | Steps both curves |
| `exclude` | `std::vector<bool>` | empty | One flag per point leaving it out (the inverse of matplotlib's `where`); a NaN also splits the fill. Kept as data: `FillData::exclude` |
| `interpolate` | `bool` | `false` | Run a region on to where the curves cross |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |

### StackOptions

Passed to `stack()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `orientation` | `Orientation` | `Horizontal` | `Horizontal` stacks up along y over x; `Vertical` along x over y |
| `baseline` | `StackBaseline` | `Zero` | Where the bottom layer starts: `Zero`, `Sym`, `Wiggle`, `WeightedWiggle` |
| `colors` | `std::vector<Color>` | empty | One per layer; empty = one cycle colour each |
| `names` | `std::vector<std::string>` | empty | One legend name per layer |
| `alpha`, `edgecolor`, `linewidth`, `linestyle`, `drawstyle` | | | As `FillOptions`, for every layer |
| `show_legend`, `visible` | `bool` | `true` | For every layer |

### QuiverOptions

Passed to `quiver()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | As `LineOptions::color`; unused with a `c` per arrow (the cycle is not advanced) |
| `alpha` | `float` | `1` | Multiplies either colour's own alpha |
| `cmap` | `Colormap` | `Viridis` | For a `c` per arrow (a cyclic map wraps) |
| `vmin`, `vmax` | `float` | `0`, `1` | Data range the colormap spans |
| `colorbar` | `bool` | `false` | Only with `c`; `name` titles it |
| `amp` | `std::optional<double>` | unset | Data units of arrow per unit of vector (matplotlib's `1/scale`). Unset = chosen from the data at the call, then kept, so animation frames stay comparable |
| `pivot` | `QuiverPivot` | `Tail` | Which part of the arrow sits on `(x, y)`: `Tail`, `Middle`, `Tip` |
| `width` | `float` | `2` | Shaft width in pixels |
| `head_width`, `head_length` | `float` | `7`, `8` | Head size in pixels; an arrow shorter than its head is all head |
| `name` | `std::string` | empty | Legend key; with `c`, also the colorbar title |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `hint_labels` | `std::vector<std::string>` | empty | Default hover text is `x=.., y=.., u=.., v=..` (plus `c=..`) |

### AxLineOptions

Passed to `axline()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `orientation` | `Orientation` | `Horizontal` | A line at y = value (`axhline`), or with `Vertical` at x = value (`axvline`) |
| `color` | `std::optional<Color>` | unset | As `LineOptions::color` |
| `linewidth` | `float` | `1.5` | |
| `linestyle` | `LineStyle` | `Solid` | `None` draws nothing and keys nothing |
| `alpha` | `float` | `1` | |
| `from`, `to` | `double` | `0`, `1` | Where it starts and ends along the frame, as fractions of it (0 = left or bottom edge), whatever the limits |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |

### AxSpanOptions

Passed to `axspan()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `orientation` | `Orientation` | `Horizontal` | A band over y (`axhspan`), or with `Vertical` over x (`axvspan`) |
| `color` | `std::optional<Color>` | unset | Unset = `Blue`; a span takes no colour from the cycle |
| `alpha` | `float` | `0.25` | |
| `edgecolor` | `std::optional<Color>` | unset | Outline; unset = `color` |
| `linewidth` | `float` | `0` | Outline width; 0 = none |
| `from`, `to` | `double` | `0`, `1` | As `AxLineOptions` |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |

### Range

An extent on one axis: a heatmap's placement, or what `xlim()` returns.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `lo`, `hi` | `double` | `0`, `1` | For a heatmap, the outer cell edges. `lo > hi` mirrors; `lo == hi` throws |

### ErrorBar

Error-bar **data**, passed before a 2D series' options. Every field is a
`std::span<const double>`, empty by default, one entry per point when given.

| Fields | Meaning |
|---|---|
| `x_cap_lo`, `x_cap_hi`, `y_cap_lo`, `y_cap_hi` | Capped whisker, as offsets below/above the point |
| `x_box_lo`, `x_box_hi`, `y_box_lo`, `y_box_hi` | Box, as offsets below/above the point |

One end given means symmetric; zero or NaN draws nothing on that side; a negative
value is read as its size.

### ErrorBarOptions

Error-bar **style**, the `errorbar` field of a 2D series' options.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Unset = the series' colour (`edgecolor` for bars, black for `scatter_z`) |
| `linewidth` | `float` | `1` | |
| `capsize` | `float` | `6` | Total cap length across a whisker end; 0 = no caps |
| `capstyle` | `CapStyle` | `Flat` | `Arrow` draws an open chevron pointing away from the point |
| `boxwidth` | `float` | `10` | Box width across the whisker, for a direction without box data |
| `box_alpha` | `float` | `0.25` | Box fill opacity, as a fraction of `color`'s; 0 = outline only |

---

## Decoration and style

### Color

| Member | Meaning |
|---|---|
| `r`, `g`, `b`, `a` | `float` in 0..1; `a` defaults to 1 |
| `Blue`, `Red`, `Green`, `Orange`, `Purple`, `Cyan` | matplotlib's tab10 colours |
| `Black`, `White`, `Gray` | |
| `from_hex(uint32_t)` | `0xRRGGBB`, or `0xRRGGBBAA` when the value exceeds `0xFFFFFF` |
| `from_name(std::string_view)` | `"#RRGGBB"`, `"#RRGGBBAA"`, or one of `red`, `blue`, `green`, `orange`, `purple`, `cyan`, `black`, `white`, `gray`/`grey`; anything else throws |

### Colormap

A value type, not an enum: a `switch` over it does not compile.

| Member | Meaning |
|---|---|
| `Viridis` (the default), `Plasma`, `Inferno`, `Magma`, `Cividis`, `Turbo`, `Coolwarm`, `Gray` | The original eight |
| `Blues`, `Greens`, `Reds`, `Oranges`, `Purples`, `Greys`, `YlOrRd`, `YlGnBu`, `Hot`, `Jet` | Sequential and classic |
| `RdBu`, `Spectral`, `BrBG`, `PiYG` | Diverging |
| `Twilight`, `TwilightShifted`, `Hsv`, `Phase` | Cyclic: values outside `[vmin, vmax]` wrap round |
| `reversed()`, `is_reversed()` | The map run backwards, like matplotlib's `_r` |
| `name()`, `is_cyclic()` | |
| `Colormap::from_name(std::string_view)` | By name, any case, with `_r` for reversed; unknown throws |
| `Colormap::names()` | Every map, registered ones included |
| `register_colormap(name, colors, cyclic = false)` | A map from two or more colours, evenly spaced, for the rest of the process |

### AxesStyle

Passed to `set_axes_style()` on `Axes` and `Axes3D`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `background` | `Color` | `White` | 2D plot-area fill, under the data; alpha 0 shows the figure background. A 3D box uses `Box3DStyle::pane_color` |
| `spine_color` | `Color` | `{0.3, 0.3, 0.3, 1}` | Frame and axis lines |
| `spine_linewidth` | `float` | `1` | |
| `spine_bottom`, `spine_left`, `spine_top`, `spine_right` | `bool` | `true` | The four 2D frame edges; ignored in 3D |
| `xaxis_color`, `yaxis_color` | `std::optional<Color>` | unset | A twinned cell: the colour of the frame edge each axis lies on (host y left, `twinx` y right; host x bottom, `twiny` x top), so an axis can take its series' colour. Unset: `spine_color` |
| `xaxis_y`, `xaxis_z` | `AxisPosition` | `Auto` | Where the x axis sits along y (and z, in 3D) |
| `yaxis_x`, `yaxis_z` | `AxisPosition` | `Auto` | Where the y axis sits along x (and z) |
| `zaxis_x`, `zaxis_y` | `AxisPosition` | `Auto` | Where the z axis sits (3D) |
| `origin_x`, `origin_y`, `origin_z` | `std::optional<double>` | unset | Put the other axes through this data value; overrides the enums |
| `frame_margin` | `float` | `0` | Space around the frame and its labels before an outside legend or colorbar |
| `show_xticks`, `show_yticks`, `show_zticks` | `bool` | `true` | `false` hides that axis' tick marks and labels and frees their room; grid lines stay. `show_zticks` is 3D only |
| `show_xtick_labels`, `show_ytick_labels` | `std::optional<bool>` | unset | 2D: show or hide an axis' tick labels, keeping its marks. Unset hides them on an inner axes of a share group |
| `tick_color` | `Color` | `{0.3, 0.3, 0.3, 1}` | |
| `tick_length` | `float` | `5` | |
| `tick_linewidth` | `float` | `1` | |
| `minor_xticks`, `minor_yticks` | `std::optional<bool>` | unset | 2D: unlabelled marks between the major ticks. Unset = on for log and symlog axes, off for linear |
| `minor_tick_length` | `float` | `2.5` | |
| `label_color` | `Color` | `{0.2, 0.2, 0.2, 1}` | Tick labels |
| `label_fontsize` | `float` | `11` | |
| `xtick_color`, `ytick_color`, `ztick_color` | `std::optional<Color>` | unset | Per axis, over `tick_color`; unset takes the shared field. `z` in 3D only |
| `xtick_length`, `ytick_length`, `ztick_length` | `std::optional<float>` | unset | Likewise for `tick_length` |
| `xtick_linewidth`, `ytick_linewidth`, `ztick_linewidth` | `std::optional<float>` | unset | Likewise for `tick_linewidth` |
| `minor_xtick_length`, `minor_ytick_length` | `std::optional<float>` | unset | Likewise for `minor_tick_length` (2D) |
| `xlabel_color`, `ylabel_color`, `zlabel_color` | `std::optional<Color>` | unset | Likewise for `label_color` |
| `xlabel_fontsize`, `ylabel_fontsize`, `zlabel_fontsize` | `std::optional<float>` | unset | Likewise for `label_fontsize` |
| `title_color`, `title_fontsize` | `Color`, `float` | `{0.15, …}`, `18` | Also set by `set_title()` |
| `xtitle_color`, `xtitle_fontsize` | `Color`, `float` | `{0.15, …}`, `16.5` | |
| `ytitle_color`, `ytitle_fontsize` | `Color`, `float` | `{0.15, …}`, `16.5` | |
| `ztitle_color`, `ztitle_fontsize` | `Color`, `float` | `{0.15, …}`, `16.5` | 3D only |
| `font_path` | `std::string` | empty | Titles and tick labels; absolute path to a .ttf/.ttc/.otf |

### ScaleOptions

Passed with a `Scale` to `set_xscale()` / `set_yscale()`; each used value must be finite.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `base` | `double` | `10` | Log and symlog; > 1 |
| `linthresh` | `double` | `1` | Symlog: the half-width of the linear part around 0; > 0 |
| `linscale` | `double` | `1` | Symlog: the length of the linear part, in decades; > 0 |

### GridOptions

Passed to `grid(enable, opts)`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `{0.8, 0.8, 0.8, 1}` | |
| `linestyle` | `LineStyle` | `Solid` | `None` draws no grid lines |
| `linewidth` | `float` | `0.5` | |
| `minor` | `bool` | `false` | 2D: also a line at each minor tick |
| `minor_color` | `Color` | `{0.9, 0.9, 0.9, 1}` | Colour of those lines |

### LegendOptions

Passed to `legend()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `anchor` | `LegendAnchor` | `OutsideRT` | Inside a frame corner, or outside the frame |
| `margin` | `float` | `10` | Distance from the anchor; space reserved for an outside legend |
| `offset_x`, `offset_y` | `float` | `0` | Move the drawn box only |
| `fontsize` | `float` | `10` | |
| `frameon` | `bool` | `true` | Draw the box |
| `text_color` | `Color` | `{0.15, 0.15, 0.15, 1}` | |
| `frame_color` | `Color` | `{1, 1, 1, 0.85}` | Box fill |
| `border_color` | `Color` | `{0.5, 0.5, 0.5, 1}` | |
| `border_linewidth` | `float` | `1` | |
| `font_path` | `std::string` | empty | Independent of `AxesStyle::font_path` |

### ColorbarOptions

Passed to `set_colorbar_style()`; styles every colorbar of the axes.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `anchor` | `ColorbarAnchor` | `Right` | Side of the frame; `Top`/`Bottom` bars are horizontal |
| `width` | `float` | `15` | Thickness across the bar |
| `margin` | `float` | `15` | Space between a bar and the frame, legend or previous bar |
| `offset_x`, `offset_y` | `float` | `0` | Move the bars as drawn |
| `fontsize` | `float` | `10` | |
| `text_color` | `Color` | `{0.2, 0.2, 0.2, 1}` | |
| `border_color` | `Color` | `{0.3, 0.3, 0.3, 1}` | |
| `border_linewidth` | `float` | `1` | |
| `font_path` | `std::string` | empty | |

---

## Text

### Pos

One coordinate of a text's position, for `Axes::text()` and `annotate()`.

| Member | Meaning |
|---|---|
| `Pos(double)` | A data value; implicit, so a plain number works |
| `Pos::fraction(double f)` | A fraction of the plot frame from its left (x) or bottom (y) edge |
| `v`, `space` | The value and its `Coords` (`Data`, `Fraction`) |

### TextOptions

Passed to `text()`, `annotate()` and the `Axes3D` ones.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `fontsize` | `float` | `12` | Pixels as drawn; constant under zoom and at any 3D distance |
| `color` | `Color` | `Black` | |
| `alpha` | `float` | `1` | Of everything the text draws (text, box, arrow) |
| `font_path` | `std::string` | empty | Empty = the default font |
| `ha` | `HAlign` | `Left` | Where the block sits against its anchor, across |
| `va` | `VAlign` | `Baseline` | ... and up/down; `Baseline` is the last line's |
| `rotation` | `float` | `0` | Degrees counter-clockwise, about the anchor |
| `dx`, `dy` | `float` | `0` | Nudge of the whole text, box included; y up |
| `linespacing` | `float` | `1.2` | Lines (`'\n'`) are `linespacing` × `fontsize` apart |
| `background` | `Color` | transparent | Box fill; alpha 0 = no fill |
| `edgecolor` | `std::optional<Color>` | unset | Box outline; unset = the text colour |
| `edge_linewidth` | `float` | `0` | 0 = no outline |
| `pad` | `float` | `4` | Box margin around the text |
| `clip_to_frame` | `bool` | `false` | Cut at the plot frame; by default a text whose data position leaves the view is hidden whole |
| `parse_math` | `bool` | `true` | Draw `$...$` as math (only when the figure's `mathtext` is on too) |

### ArrowOptions

The arrow `annotate()` draws from its text to the point.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `head` | `ArrowHead` | `Filled` | At the point |
| `tail` | `ArrowHead` | `None` | At the text |
| `head_length`, `head_width` | `float` | `10`, `7` | |
| `linewidth` | `float` | `1.25` | |
| `color` | `std::optional<Color>` | unset | Unset = the text colour |
| `linestyle` | `LineStyle` | `Solid` | |
| `gap_text`, `gap_point` | `float` | `2`, `0` | Pixels left clear at the text's box and before the point |
| `arc` | `float` | `0` | 0 = straight; otherwise it bows to one side by `arc` × its length (negative: the other side), as matplotlib's `arc3` |

---

## Events

### Event

What a callback registered with `Figure::connect()` receives. One flat record; the
`kind` says which fields mean anything. Valid only during the call.

| Field | Type | Kinds | Meaning |
|---|---|---|---|
| `kind` | `EventKind` | all | |
| `mods` | `int` | mouse, scroll, key | `EventMods` bits: `kModCtrl`, `kModShift`, `kModAlt`, `kModSuper` |
| `button` | `int` | `MouseDown/Up` | 0 left, 1 right, 2 middle |
| `double_click` | `bool` | `MouseDown` | |
| `x`, `y` | `float` | mouse, scroll | Logical pixels from the top left of the plot area |
| `axes` | `int` | mouse, scroll | The subplot under the cursor (as `add_subplot()` numbers it; a span by its first cell); -1 outside every frame |
| `has_data`, `xdata`, `ydata`, `zdata` | `bool`, `double` | mouse, scroll | The data point under the cursor; in 3D where the cursor meets the nearest visible `Plane2D`, else `has_data` is false |
| `x2data`, `y2data` | `double` | mouse, scroll | A 2D axes with twins: the `twiny()`'s x and the `twinx()`'s y under the cursor (NaN without that twin) |
| `scroll_x`, `scroll_y` | `double` | `Scroll` | Notches; positive y = away from the user |
| `consumed` | `EventConsumed` | mouse | What sextant did with the input: `None`, `Select`, `Navigate`, `GridDrag`. Informational |
| `key` | `std::string` | `KeyDown/Up` | `"a"`, `"A"` (shift), `"ctrl+a"`, `"escape"`, `"f5"`, `"left"`, …; modifiers as `ctrl+`, `alt+`, `super+`, and `shift+` for keys other than letters |
| `width`, `height` | `int` | `Resize` | The new plot area |
| `pick_kind` | `PickKind` | `Pick` | What was hit |
| `pick_object` | `int` | `Pick` | Its index within its kind (on the axes, or on the plane) |
| `pick_index` | `int` | `Pick` | Point, bar, marker or vertex; a surface sample; the flat cell index of a heatmap or `bar3d`; a fill's segment *i* (between its points *i* and *i* + 1) |
| `pick_row`, `pick_col` | `int` | `Pick` | For heatmap and `bar3d` grids, else -1 |
| `pick_plane` | `int` | `Pick` | The plane's index in `Axes3D::plane_at()`, or -1 |
| `pick_twin` | `int` | `Pick` | In a cell with twins, whose object `pick_object` indexes: 0 the axes' own, 1 its `twinx()`, 2 its `twiny()` |

---

## 3D options

### Camera3D

Passed to `set_camera()` and `set_default_camera()`; returned by `camera()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `azimuth` | `double` | `-60` | Degrees about the box's z axis; 0 looks along +x |
| `elevation` | `double` | `30` | Degrees above the xy plane, clamped to ±89 |
| `target` | `Vec3` | `{0, 0, 0}` | Box-space point the camera orbits |
| `zoom` | `double` | `1` | Magnifies the fitted picture |
| `projection` | `Projection` | `Orthographic` | |
| `fov` | `double` | `45` | Vertical field of view, perspective only, 5..120 |

### Vec3

| Field | Type | Default |
|---|---|---|
| `x`, `y`, `z` | `double` | `0` |

### BoxAspect

Passed to `set_box_aspect()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `x`, `y`, `z` | `double` | `1` | Relative side lengths of the box |

### Box3DStyle

Passed to `set_box_style()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `panes` | `bool` | `true` | Draw the three back panes |
| `pane_color` | `Color` | `{0.94, 0.94, 0.96, 1}` | |
| `pane_edge_color` | `Color` | `{0.75, 0.75, 0.78, 1}` | |
| `margin` | `float` | `0.12` | Fraction of the cell left around the box for labels |

### Plane2DOptions

Passed to `Axes3D::plane()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `alpha` | `float` | `1` | Whole-plane opacity, multiplying its objects' own |
| `visible` | `bool` | `true` | Hides the drawing only; limits, colorbars and legend keys stay (unlike hiding one object, which leaves them) |

### Bar3DOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Unset = the scene's next colour-cycle colour (with `colormap` on, the next, without advancing) |
| `alpha` | `float` | `1` | |
| `width`, `depth` | `float` | `0.8` | Footprint as a fraction of the grid spacing along u and v |
| `bottom` | `double` | `0` | Base of every bar, unless `bottoms` is passed |
| `shading` | `float` | `0.45` | 0 = flat colour, 1 = the dimmest face black |
| `edges` | `bool` | `false` | Outline the bars |
| `edgecolor` | `Color` | `Black` | |
| `edge_alpha` | `float` | `1` | Independent of `alpha` |
| `edge_linewidth` | `float` | `1` | At the bar's depth |
| `name` | `std::string` | empty | Legend key (a swatch of `color`) |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `hint_labels` | `std::vector<std::string>` | empty | Per bar, indexed like `heights` |

### SurfaceOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Flat colour, unless `colormap`; unset = the next cycle colour |
| `colormap` | `bool` | `false` | Colour each cell by height |
| `cmap` | `Colormap` | `Viridis` | |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the surface's own range |
| `colorbar` | `bool` | `false` | Only with `colormap` |
| `name` | `std::string` | empty | Colorbar title when colour-mapped, else legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `alpha` | `float` | `1` | |
| `shading` | `float` | `0.45` | |
| `edges` | `bool` | `false` | Wireframe along the sampling grid |
| `edgecolor` | `Color` | `Black` | |
| `edge_alpha` | `float` | `1` | |
| `edge_linewidth` | `float` | `1` | At the box centre |
| `hint_labels` | `std::vector<std::string>` | empty | Per sample, indexed like `heights` |

### SurfaceTriOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Flat colour, unless `colors` is passed; unset = the next cycle colour |
| `cmap` | `Colormap` | `Viridis` | For a `colors` vector |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the mesh's own range |
| `colorbar` | `bool` | `false` | Only with `colors` |
| `name` | `std::string` | empty | Colorbar title when colour-mapped, else legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `alpha` | `float` | `1` | |
| `shading` | `float` | `0.45` | Per face, both sides lit |
| `edges` | `bool` | `false` | All three edges of every triangle |
| `edgecolor` | `Color` | `Black` | |
| `edge_alpha` | `float` | `1` | |
| `edge_linewidth` | `float` | `1` | At the box centre |
| `hint_labels` | `std::vector<std::string>` | empty | Per vertex |

### Scatter3DOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Flat colour, unless `colors` is passed; unset = the next cycle colour |
| `size` | `float` | `20` | Marker diameter, constant at any depth |
| `sizes` | `std::vector<double>` | empty | One diameter per point, as `ScatterOptions::sizes`; `Scatter3DData::sizes` |
| `marker` | `MarkerStyle` | `Circle` | |
| `alpha` | `float` | `1` | |
| `edgecolor`, `edge_alpha`, `edge_linewidth` | | unset, `1`, `0` | Marker outline, as in `ScatterOptions`; darkened with `depthshade` |
| `depthshade` | `float` | `0` | Darken with distance; 0 = off |
| `cmap` | `Colormap` | `Viridis` | For a `colors` vector |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the series' own range |
| `colorbar` | `bool` | `false` | Only with `colors` |
| `name` | `std::string` | empty | Legend key; with `colors`, also the colorbar title |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `errorbar` | `ErrorBar3DOptions` | | |
| `hint_labels` | `std::vector<std::string>` | empty | |

### Line3DOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Flat colour, unless `colors` is passed; unset = the next cycle colour |
| `linewidth` | `float` | `1.5` | At the box centre; thinner with distance under perspective |
| `alpha` | `float` | `1` | |
| `loop` | `bool` | `false` | Close the path |
| `depthshade` | `float` | `0` | |
| `cmap` | `Colormap` | `Viridis` | For a `colors` vector |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the path's own range |
| `colorbar` | `bool` | `false` | Only with `colors` |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `visible` | `bool` | `true` | As `LineOptions::visible` |
| `errorbar` | `ErrorBar3DOptions` | | |
| `hint_labels` | `std::vector<std::string>` | empty | Per vertex |

### ErrorBar3D

Error-bar **data** in a scene, passed before a `scatter3d`/`line3d` series'
options. Every field is a `std::span<const double>`, empty by default.

| Fields | Meaning |
|---|---|
| `x_cap_lo`, `x_cap_hi`, `y_cap_lo`, `y_cap_hi`, `z_cap_lo`, `z_cap_hi` | Capped whisker offsets |
| `x_box_lo`, `x_box_hi`, `y_box_lo`, `y_box_hi`, `z_box_lo`, `z_box_hi` | Box offsets |

Same rules as `ErrorBar`.

### ErrorBar3DOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `std::optional<Color>` | unset | Unset = the series' colour, or black with `colors` |
| `linewidth` | `float` | `1` | 0 draws no error bar at all |
| `capsize` | `float` | `6` | |
| `capstyle` | `CapStyle` | `Flat` | |
| `boxwidth` | `float` | `10` | Block extent on axes without box data |
| `box_alpha` | `float` | `0.25` | Block faces |
| `edge_alpha` | `float` | `0.5` | Block edges |

Pixel sizes here are measured at the box centre.

---

## Enums

| Enum | Values |
|---|---|
| `LineStyle` | `Solid`, `Dashed`, `Dotted`, `DashDot`, `None` |
| `DrawStyle` | `Default`, `StepsPre`, `StepsMid`, `StepsPost` |
| `Orientation` | `Vertical`, `Horizontal` |
| `MarkerStyle` | `None`, `Circle`, `Square`, `Triangle`, `Cross`, `Plus`, `Diamond`, `TriangleDown`, `TriangleLeft`, `TriangleRight`, `Star`, `Pentagon`, `Hexagon`, `ThinCross`, `ThinPlus`, `VLine`, `HLine` |
| `Scale` | `Linear`, `Log`, `Symlog` |
| `Adjustable` | `Box`, `Datalim` |
| `StackBaseline` | `Zero`, `Sym`, `Wiggle`, `WeightedWiggle` |
| `QuiverPivot` | `Tail`, `Middle`, `Tip` |
| `MathFontset` | `Text`, `Stix`, `Cm`, `Custom` |
| `CapStyle` | `Flat`, `Arrow` |
| `AxisPosition` | `Auto` (bottom/left in 2D; the box silhouette in 3D), `Low`, `Mid`, `High` |
| `LegendAnchor` | `InsideTL`, `InsideTR`, `InsideBL`, `InsideBR`, `OutsideTL`, `OutsideTR`, `OutsideBL`, `OutsideBR`, `OutsideLT`, `OutsideLB`, `OutsideRT`, `OutsideRB` |
| `ColorbarAnchor` | `Left`, `Right`, `Top`, `Bottom` |
| `HAlign` | `Left`, `Center`, `Right` |
| `VAlign` | `Top`, `Center`, `Baseline`, `Bottom` |
| `Coords` | `Data`, `Fraction` |
| `ArrowHead` | `None`, `Open`, `Filled`, `Bar` |
| `EventKind` | `Close`, `MouseDown`, `MouseUp`, `MouseMove`, `Scroll`, `KeyDown`, `KeyUp`, `Resize`, `Pick` |
| `PickKind` | `None`, `Line`, `Scatter`, `ScatterZ`, `Bar`, `Heatmap`, `Bar3D`, `Surface`, `SurfaceTri`, `Scatter3D`, `Line3D`, `Quiver`, `Fill` |
| `EventConsumed` | `None`, `Select`, `Navigate`, `GridDrag` |
| `PanelTheme` | `Dark`, `Light`, `Classic` |
| `Projection` | `Orthographic`, `Perspective` |
| `PlaneOrientation` | `XY`, `YZ`, `ZX` |

`LegendAnchor`: `Inside*` sits in a corner of the frame and reserves no space.
`OutsideT*`/`OutsideB*` sit above or below the frame, flush left or right, with
entries in wrapping rows; `OutsideL*`/`OutsideR*` sit left or right, flush top or
bottom, with entries in a column.

---

## Data structs

Returned by `<kind>_data(i)` and accepted by `set_<kind>_data(i, …)`. Every
field is a `std::vector` unless noted.

| Struct | Fields |
|---|---|
| `LineData` | `x`, `y` (from `line(y)`, x is 0, 1, 2, …) |
| `ScatterData` | `x`, `y`, `sizes` (empty = the options' `size`) |
| `ScatterZData` | `x`, `y`, `z`, `sizes` |
| `BarData` | `x`, `height` (from `hist()`, bin centres and heights), `bottom` (empty = every bar on 0) |
| `FillData` | `x`, `y1`, `y2`, `exclude` (`std::vector<bool>`, empty = none); for `Vertical`, `x` holds y and `y1`, `y2` the x curves. A `stack()` reads back as one per layer |
| `QuiverData` | `x`, `y`, `u`, `v`, `c` (empty = a flat colour) |
| `AxLineData` | `value` (`double`) |
| `AxSpanData` | `lo`, `hi` (`double`) |
| `HeatmapData` | `data` (row-major), `rows`, `cols` (`int`), `xrange`, `yrange` (`Range`). Values come back rounded to `float` |
| `Bar3DData` | `orient` (`PlaneOrientation`), `u`, `v`, `heights`, `bottoms` (empty = every bar on `bottom`) |
| `SurfaceData` | `orient`, `u`, `v`, `heights` |
| `SurfaceTriData` | `x`, `y`, `z`, `tri` (`std::uint32_t`; the derived triangulation if one was), `colors` (empty = flat) |
| `Scatter3DData` | `x`, `y`, `z`, `colors` (empty = flat), `sizes` |
| `Line3DData` | `x`, `y`, `z`, `colors` (empty = flat) |
| `TextData` | `text` (`std::string`), `x`, `y` (`Pos`), `arrow` (`bool`), `px`, `py` (`double`, the arrow's point) |
| `Text3DData` | `text`, `x`, `y`, `z` (`double`), `in_frame` (`bool`, a `text2d()`: x, y are frame fractions), `arrow` (`bool`, an `annotate()`), `dx`, `dy` (`double`, its pixel offset) |
