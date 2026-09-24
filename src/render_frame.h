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

// One axes' resolved layout for the frame just rendered, reused by pan/zoom and
// hit-testing so they match what was drawn.
struct AxesLayout {
    AxesSlot       slot;
    CoordTransform tr;

    // 3D cells only (`tr` is unused there): the projector the frame was drawn with.
    std::optional<Projector3D> proj3d;

    // The whole subplot including decorations; used for click selection and
    // the selection outline.
    PlotRect cell{};
};

// Renders fsnap into the currently bound framebuffer, laid out at target_w x
// target_h logical pixels (<= 0: ctx's size). Only rasterization is scaled, by
// `pixel_ratio` -- the display scale times the supersample factor, so not
// necessarily whole -- and the caller must allocate the target at that scale
// (and filter any supersampling down). `out_layout` receives one entry per
// axes. `layout`, if given, is a precomputed layout at this size; null lays out
// afresh.
void render_frame(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                  const FigureSnapshot& fsnap, int target_w = -1, int target_h = -1,
                  float pixel_ratio = 1.0f,
                  std::vector<AxesLayout>* out_layout = nullptr,
                  const FigureLayout* layout = nullptr);
} // namespace sextant
