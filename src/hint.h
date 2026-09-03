#pragma once
#include "plot_objects.h"
#include "coord_transform.h"
#include "hint_index.h"
#include "render_frame.h"
#include <optional>
#include <string>
#include <vector>

namespace sextant {

// Fixed pixel-distance hit threshold for point-based nearest search (Step
// 19 mouse hint) — not exposed as a configurable option in v1.
constexpr float kHintHitRadiusPx = 12.0f;

struct HintResult {
    std::string text;                  // may contain embedded '\n'
    float       anchor_x = 0, anchor_y = 0;  // physical-pixel anchor point
};

// Cursor (physical pixels) -> which axes cell it's over, by testing against
// each entry's tr.px/py/pw/ph rect. nullptr if over no cell (e.g. margins).
const AxesLayout* find_hint_cell(const std::vector<AxesLayout>& layout,
                                  float cursor_x, float cursor_y);

// Within one axes (snap/tr must correspond to the same axes), finds the single
// globally-nearest point across line/scatter/scatter_z/bar within
// kHintHitRadiusPx of the cursor, falling back to heatmap cell containment.
// nullopt = nothing under the cursor.
//
// `index` is an optional spatial index (hint_index.h) with set_frame_key()
// already called for this snapshot and axes. Passing nullptr simply scans; the
// result is identical either way.
std::optional<HintResult> find_hint(const RenderSnapshot& snap,
                                    const CoordTransform& tr,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index = nullptr);

} // namespace sextant
