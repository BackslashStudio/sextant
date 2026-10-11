# Third-party notices

sextant itself is MIT licensed (see [LICENSE](LICENSE)). It vendors, fetches or
links the components below. All of them are permissive; none imposes a copyleft
obligation on your use of sextant. This file exists because several of them ask
that their notice travel with the software, and because FreeType asks to be
credited in documentation.

If you ship a binary that embeds sextant, ship this file with it.

---

## Vendored into `third_party/` (source in this repository)

| Component | License | Notes |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) (`v1.92.8-docking`) | MIT | The Controls/Data panel UI. Docking branch, not mainline. |
| [NanoVG](https://github.com/memononen/nanovg) | zlib | Vector rendering for axes, text and decorations. Bundles `fontstash.h` and `stb_truetype.h`. |
| [stb](https://github.com/nothings/stb) (`stb_truetype.h`, `stb_image_write.h`) | Public domain (or MIT, at your option) | Font rasterization fallback and PNG output fallback. |
| [GLAD](https://glad.dav1d.de/) (v1, OpenGL 4.1 core) | Generator MIT; generated loader public domain. Includes Khronos `khrplatform.h` (MIT). | OpenGL function loader. |
| [Roboto](https://fonts.google.com/specimen/Roboto) (`Roboto-Medium.ttf`) | Apache License 2.0 | Panel UI font, compiled into `src/widgets/panel_font.h`. Separately licensed from Dear ImGui, which merely distributes it. |

## Fetched at configure time

| Component | License | Notes |
|---|---|---|
| [GLFW](https://www.glfw.org/) 3.4 | zlib/libpng | Window and input. Built from source via `FetchContent` and statically linked. **Modified**: patched for macOS by `cmake/glfw/*.patch` (changes marked `sextant:` in the source). |

## Linked from the system (or vcpkg)

| Component | License | Notes |
|---|---|---|
| [FreeType](https://freetype.org/) | The FreeType Project License (BSD-style), **or** GPLv2 at your option | Glyph rasterization. sextant uses it under the FTL; the GPLv2 alternative is not elected. Optional — `SEXTANT_USE_FREETYPE=OFF` falls back to stb_truetype. |
| [libpng](http://www.libpng.org/pub/png/libpng.html) | libpng License (permissive) | PNG output. Optional — `SEXTANT_USE_LIBPNG=OFF` falls back to stb_image_write. |
| [zlib](https://zlib.net/) | zlib License | Pulled in by libpng. |
| [GLM](https://github.com/g-truc/glm) | MIT (Happy Bunny / MIT dual) | Header-only math. |

## Colormap data (compiled into `src/colormaps.cpp` and `src/colormap_tables.h`)

The built-in colormaps are 256-entry tables sampled from these sources. Only
the color values are used; no source code.

| Maps | Source | License |
|---|---|---|
| viridis, plasma, inferno, magma | [matplotlib](https://matplotlib.org/); by Nathaniel J. Smith and Stéfan van der Walt (viridis with Eric Firing) | CC0 1.0 |
| cividis, coolwarm, gray, jet, hot, hsv | [matplotlib](https://matplotlib.org/) (cividis by Jamie R. Nuñez et al., coolwarm by Kenneth Moreland) | matplotlib License (PSF-based, BSD-compatible) |
| twilight, twilight_shifted | [matplotlib](https://matplotlib.org/); by Bastian Bechtold | matplotlib License |
| turbo | Google LLC (Anton Mikhailov), via matplotlib | Apache License 2.0 |
| Blues, Greens, Reds, Oranges, Purples, Greys, YlOrRd, YlGnBu, RdBu, Spectral, BrBG, PiYG | [ColorBrewer](https://colorbrewer2.org/), Cynthia Brewer, Mark Harrower and The Pennsylvania State University, via matplotlib | Apache License 2.0 |
| phase | [cmocean](https://matplotlib.org/cmocean/), Copyright (c) 2015 Kristen M. Thyng | MIT |

---

## Required credit

> Portions of this software are copyright © 2026 The FreeType Project
> (www.freetype.org). All rights reserved.

This acknowledgement is required by the FreeType Project License whenever
FreeType is used, which is sextant's default configuration
(`SEXTANT_USE_FREETYPE=ON`).

> This product includes color specifications and designs developed by Cynthia
> Brewer (http://colorbrewer.org/).

The attribution ColorBrewer asks for, for the ColorBrewer colormaps above.

## Notice retention

The zlib, libpng and MIT licenses above require that their copyright notice and
permission notice travel with any redistribution, in source or binary form. The
notices are present in each vendored file's own header comment; keep them there,
and keep this file alongside any binary distribution.
