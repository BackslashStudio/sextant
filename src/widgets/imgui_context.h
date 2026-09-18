#pragma once

#include "sextant/figure.h"   // PanelTheme, kept for the stored theme

struct ImGuiContext;

namespace sextant {
class GLContext;

// Writes the panel style for `theme` at chrome scale `scale` (1.0 at 100%,
// 1.5 at 150%) into the current ImGui context's style. A pure function of its
// two arguments and idempotent in `scale`: it builds from a default-
// constructed ImGuiStyle rather than from the live one, because
// ScaleAllSizes() multiplies in place and StyleColorsX() writes only Colors[],
// so re-scaling the style already in effect would compound 1.5 into 2.25.
// Free rather than a private member so it can be asserted without a window.
void apply_panel_style(PanelTheme theme, float scale);

// RAII owner of exactly one Dear ImGui context bound to one GLContext's
// window. Must be constructed and destroyed on that GLContext's thread,
// strictly after it exists and strictly before it is torn down. Never
// construct one for a headless GLContext -- there is no panel there.
//
// The theme is fixed at construction from `opts`. The HiDPI chrome scale is
// not: sync_dpi_scale() re-derives it every frame from the window's current
// monitor, so dragging a figure between displays of different DPI rescales the
// panel rather than leaving it at whatever the monitor it opened on wanted.
class ImGuiPanelContext {
public:
    ImGuiPanelContext(GLContext& ctx, const FigureOptions& opts);
    ~ImGuiPanelContext();

    ImGuiPanelContext(const ImGuiPanelContext&) = delete;
    ImGuiPanelContext& operator=(const ImGuiPanelContext&) = delete;

    // Re-points ImGui's implicit context pointer at this instance's context.
    // With the thread-local GImGui from sextant_imconfig.h each window thread
    // already keeps its own, so this is belt-and-braces rather than load-
    // bearing per frame — but it is what makes that an invariant of the frame
    // loop instead of an accident of construction order.
    void make_current() const;

    // Re-scales the panel chrome if `ctx`'s window has moved to a monitor of a
    // different content scale. Call once per frame, before NewFrame(); a no-op
    // (one GLFW query and a float compare) on every frame but the ones where
    // the scale actually changed.
    void sync_dpi_scale(const GLContext& ctx);

    // The scale currently applied to the chrome: 1.0 at 100%, 1.5 at 150%.
    // Anything sized in window pixels rather than in style units has to
    // multiply by this or it stays physically small (see ensure_layout()).
    float dpi_scale() const { return dpi_scale_; }

private:
    ImGuiContext* ctx_       = nullptr;
    PanelTheme    theme_     = PanelTheme::Light;
    float         dpi_scale_ = 1.0f;
};

} // namespace sextant
