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
  [SvgSaveReport](#svgsavereport) · [FrameStats](#framestats)
- [2D plot options](#2d-plot-options): [LineOptions](#lineoptions) ·
  [ScatterOptions](#scatteroptions) · [ScatterZOptions](#scatterzoptions) ·
  [BarOptions](#baroptions) · [HistOptions](#histoptions) ·
  [HeatmapOptions](#heatmapoptions) · [Range](#range) · [ErrorBar](#errorbar) ·
  [ErrorBarOptions](#errorbaroptions)
- [Decoration and style](#decoration-and-style): [Color](#color) ·
  [AxesStyle](#axesstyle) · [GridOptions](#gridoptions) ·
  [LegendOptions](#legendoptions) · [ColorbarOptions](#colorbaroptions)
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
| `color` | `Color` | `Blue` | |
| `linewidth` | `float` | `1.5` | |
| `linestyle` | `LineStyle` | `Solid` | `None` draws no stroke and drops the series from the legend |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | Keyed when this is true and `name` is not empty |
| `alpha` | `float` | `1` | |
| `loop` | `bool` | `false` | Add a closing segment from the last point to the first |
| `errorbar` | `ErrorBarOptions` | | Style of the error bars passed as an `ErrorBar` |
| `hint_labels` | `std::vector<std::string>` | empty | Extra hover text per point |

### ScatterOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `Blue` | |
| `size` | `float` | `20` | Marker diameter; constant under zoom |
| `marker` | `MarkerStyle` | `Circle` | |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `alpha` | `float` | `0.8` | |
| `errorbar` | `ErrorBarOptions` | | |
| `hint_labels` | `std::vector<std::string>` | empty | |

### ScatterZOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `cmap` | `Colormap` | `Viridis` | |
| `size` | `float` | `20` | |
| `marker` | `MarkerStyle` | `Circle` | |
| `alpha` | `float` | `0.8` | |
| `vmin`, `vmax` | `float` | `0`, `1` | Data range the colormap spans |
| `colorbar` | `bool` | `false` | Draw a colorbar for this series |
| `name` | `std::string` | empty | Titles the colorbar; also the legend key (marker filled white, black edge) |
| `show_legend` | `bool` | `true` | |
| `errorbar` | `ErrorBarOptions` | | Unset colour = black |
| `hint_labels` | `std::vector<std::string>` | empty | Default hover text is `x=.., y=.., z=..` |

### BarOptions

Also the bar style of `hist()`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `Blue` | |
| `width` | `float` | `0.8` | Fraction of the bar spacing (for `hist()`, of the bin width) |
| `alpha` | `float` | `1` | |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
| `edgecolor` | `Color` | `Black` | |
| `linewidth` | `float` | `0.5` | Edge width |
| `errorbar` | `ErrorBarOptions` | | Hung off the bar's tip; unset colour = `edgecolor` |
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
| `origin` | `std::string` | `"lower"` | `"lower"` draws row 0 at the bottom, `"upper"` at the top |
| `contours` | `std::vector<double>` | empty | Contour levels in data units |
| `contour_color` | `Color` | `Black` | |
| `contour_linewidth` | `float` | `1` | |
| `contour_labels` | `bool` | `false` | Write each level on its line |
| `contour_fontsize` | `float` | `10` | |
| `hint_labels` | `std::vector<std::string>` | empty | Row-major, `row * cols + col` |

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
| `from_hex(uint32_t)` | `0xRRGGBB` or `0xRRGGBBAA` |
| `from_name(std::string_view)` | A basic colour name, or `"#RRGGBB"`; throws on an unknown one |

### AxesStyle

Passed to `set_axes_style()` on `Axes` and `Axes3D`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `spine_color` | `Color` | `{0.3, 0.3, 0.3, 1}` | Frame and axis lines |
| `spine_linewidth` | `float` | `1` | |
| `spine_bottom`, `spine_left`, `spine_top`, `spine_right` | `bool` | `true` | The four 2D frame edges; ignored in 3D |
| `xaxis_y`, `xaxis_z` | `AxisPosition` | `Auto` | Where the x axis sits along y (and z, in 3D) |
| `yaxis_x`, `yaxis_z` | `AxisPosition` | `Auto` | Where the y axis sits along x (and z) |
| `zaxis_x`, `zaxis_y` | `AxisPosition` | `Auto` | Where the z axis sits (3D) |
| `origin_x`, `origin_y`, `origin_z` | `std::optional<double>` | unset | Put the other axes through this data value; overrides the enums |
| `frame_margin` | `float` | `0` | Space around the frame and its labels before an outside legend or colorbar |
| `tick_color` | `Color` | `{0.3, 0.3, 0.3, 1}` | |
| `tick_length` | `float` | `5` | |
| `tick_linewidth` | `float` | `1` | |
| `label_color` | `Color` | `{0.2, 0.2, 0.2, 1}` | Tick labels |
| `label_fontsize` | `float` | `11` | |
| `title_color`, `title_fontsize` | `Color`, `float` | `{0.15, …}`, `18` | Also set by `set_title()` |
| `xtitle_color`, `xtitle_fontsize` | `Color`, `float` | `{0.15, …}`, `16.5` | |
| `ytitle_color`, `ytitle_fontsize` | `Color`, `float` | `{0.15, …}`, `16.5` | |
| `ztitle_color`, `ztitle_fontsize` | `Color`, `float` | `{0.15, …}`, `16.5` | 3D only |
| `font_path` | `std::string` | empty | Titles and tick labels; absolute path to a .ttf/.ttc/.otf |

### GridOptions

Passed to `grid(enable, opts)`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `{0.8, 0.8, 0.8, 1}` | |
| `linestyle` | `LineStyle` | `Solid` | `None` draws no grid lines |
| `linewidth` | `float` | `0.5` | |

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
| `visible` | `bool` | `true` | Hides the drawing only; limits, colorbars and legend keys stay |

### Bar3DOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `Blue` | |
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
| `hint_labels` | `std::vector<std::string>` | empty | Per bar, indexed like `heights` |

### SurfaceOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `Blue` | Flat colour, unless `colormap` |
| `colormap` | `bool` | `false` | Colour each cell by height |
| `cmap` | `Colormap` | `Viridis` | |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the surface's own range |
| `colorbar` | `bool` | `false` | Only with `colormap` |
| `name` | `std::string` | empty | Colorbar title when colour-mapped, else legend key |
| `show_legend` | `bool` | `true` | |
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
| `color` | `Color` | `Blue` | Flat colour, unless `colors` is passed |
| `cmap` | `Colormap` | `Viridis` | For a `colors` vector |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the mesh's own range |
| `colorbar` | `bool` | `false` | Only with `colors` |
| `name` | `std::string` | empty | Colorbar title when colour-mapped, else legend key |
| `show_legend` | `bool` | `true` | |
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
| `color` | `Color` | `Blue` | Flat colour, unless `colors` is passed |
| `size` | `float` | `20` | Marker diameter, constant at any depth |
| `marker` | `MarkerStyle` | `Circle` | |
| `alpha` | `float` | `1` | |
| `depthshade` | `float` | `0` | Darken with distance; 0 = off |
| `cmap` | `Colormap` | `Viridis` | For a `colors` vector |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the series' own range |
| `colorbar` | `bool` | `false` | Only with `colors` |
| `name` | `std::string` | empty | Legend key; with `colors`, also the colorbar title |
| `show_legend` | `bool` | `true` | |
| `errorbar` | `ErrorBar3DOptions` | | |
| `hint_labels` | `std::vector<std::string>` | empty | |

### Line3DOptions

| Field | Type | Default | Meaning |
|---|---|---|---|
| `color` | `Color` | `Blue` | Flat colour, unless `colors` is passed |
| `linewidth` | `float` | `1.5` | At the box centre; thinner with distance under perspective |
| `alpha` | `float` | `1` | |
| `loop` | `bool` | `false` | Close the path |
| `depthshade` | `float` | `0` | |
| `cmap` | `Colormap` | `Viridis` | For a `colors` vector |
| `vmin`, `vmax` | `float` | `0`, `0` | Equal = the path's own range |
| `colorbar` | `bool` | `false` | Only with `colors` |
| `name` | `std::string` | empty | Legend key |
| `show_legend` | `bool` | `true` | |
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
| `MarkerStyle` | `None`, `Circle`, `Square`, `Triangle`, `Cross`, `Plus`, `Diamond` |
| `Colormap` | `Viridis`, `Plasma`, `Inferno`, `Magma`, `Cividis`, `Turbo`, `Coolwarm`, `Gray` |
| `CapStyle` | `Flat`, `Arrow` |
| `AxisPosition` | `Auto` (bottom/left in 2D; the box silhouette in 3D), `Low`, `Mid`, `High` |
| `LegendAnchor` | `InsideTL`, `InsideTR`, `InsideBL`, `InsideBR`, `OutsideTL`, `OutsideTR`, `OutsideBL`, `OutsideBR`, `OutsideLT`, `OutsideLB`, `OutsideRT`, `OutsideRB` |
| `ColorbarAnchor` | `Left`, `Right`, `Top`, `Bottom` |
| `HAlign` | `Left`, `Center`, `Right` |
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
| `ScatterData` | `x`, `y` |
| `ScatterZData` | `x`, `y`, `z` |
| `BarData` | `x`, `height` (from `hist()`, bin centres and heights) |
| `HeatmapData` | `data` (row-major), `rows`, `cols` (`int`), `xrange`, `yrange` (`Range`). Values come back rounded to `float` |
| `Bar3DData` | `orient` (`PlaneOrientation`), `u`, `v`, `heights`, `bottoms` (empty = every bar on `bottom`) |
| `SurfaceData` | `orient`, `u`, `v`, `heights` |
| `SurfaceTriData` | `x`, `y`, `z`, `tri` (`std::uint32_t`; the derived triangulation if one was), `colors` (empty = flat) |
| `Scatter3DData` | `x`, `y`, `z`, `colors` (empty = flat) |
| `Line3DData` | `x`, `y`, `z`, `colors` (empty = flat) |
