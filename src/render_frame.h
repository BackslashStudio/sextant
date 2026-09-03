#pragma once
#include "plot_objects.h"
#include "coord_transform.h"
#include <vector>

namespace sextant {
class GLContext;
class NvgRenderer;
class DataRenderer;

// One axes' resolved on-screen layout for the frame just rendered — the
// widget panel's pan/zoom reuses this instead of re-deriving
// margin/legend/colorbar carve-out math, so hit-testing/drag-to-data-delta
// conversion always matches exactly what was actually drawn.
struct AxesLayout {
    AxesSlot       slot;
    CoordTransform tr;
};

// Renders fsnap into whatever framebuffer is currently bound, sized
// target_w x target_h; omit (<=0) to use ctx's own size. When the widget panel
// is active the caller binds an offscreen PlotFbo before calling this, so
// render_frame() itself stays unaware that a panel exists.
//
// target_w/target_h stay in *logical* pixels, and all layout math -- margins,
// font sizes, tick lengths, the returned out_layout -- stays in that space, so
// hit-testing against it needs no scaling. Only the rasterization is enlarged
// by `supersample`: this sets a viewport that many times bigger in each axis
// and tells NanoVG and DataRenderer the device-pixel ratio. The *caller* owns
// the render target, must have allocated it at target_w*supersample x
// target_h*supersample, and is responsible for filtering it back down.
//
// out_layout, when non-null, is filled with one entry per axes: its grid slot
// and resolved CoordTransform.
void render_frame(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                  const FigureSnapshot& fsnap, int target_w = -1, int target_h = -1,
                  int supersample = 1,
                  std::vector<AxesLayout>* out_layout = nullptr);
} // namespace sextant
