#pragma once
#include "plot_objects.h"
#include "coord_transform.h"
#include "coord_transform3d.h"
#include "renderer/plot_rect.h"
#include <optional>
#include <vector>

namespace sextant {
class GLContext;
class NvgRenderer;
class DataRenderer;
struct FigureLayout;

// One axes' resolved on-screen layout for the frame just rendered — the
// widget panel's pan/zoom reuses this instead of re-deriving
// margin/legend/colorbar carve-out math, so hit-testing/drag-to-data-delta
// conversion always matches exactly what was actually drawn.
struct AxesLayout {
    AxesSlot       slot;
    CoordTransform tr;

    // Present exactly for a 3D cell, where `tr` means nothing. Navigation
    // needs the camera's own basis to strafe across the screen rather than
    // along a data axis, and this is the projector the frame was actually
    // drawn with -- the same reason `tr` is handed back rather than
    // re-derived.
    std::optional<Projector3D> proj3d;

    // The whole subplot -- frame plus its titles, tick labels, legend and
    // colorbar -- i.e. the grid's share of the figure before any inset. What a
    // click selects by, and what the selection outline is drawn around.
    PlotRect cell{};
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
//
// `layout`, when non-null, is the figure's layout at target_w x target_h,
// already computed by the caller -- the window's, from its LayoutStore, or an
// export's, from the measurements on screen (v1.0 step 15.2). Null lays the
// snapshot out afresh.
void render_frame(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                  const FigureSnapshot& fsnap, int target_w = -1, int target_h = -1,
                  int supersample = 1,
                  std::vector<AxesLayout>* out_layout = nullptr,
                  const FigureLayout* layout = nullptr);
} // namespace sextant
