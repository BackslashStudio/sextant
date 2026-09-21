#pragma once

#include "sextant/figure.h"   // PanelTheme, kept for the stored theme

struct ImGuiContext;

namespace sextant {
    class GLContext;

    // Writes the panel style for `theme` at chrome scale `scale` (1.0 = 100%).
    // Built from a default ImGuiStyle so repeated calls don't compound scaling.
    void apply_panel_style(PanelTheme theme, float scale);

    // RAII owner of one ImGui context for one GLContext's window. Create and
    // destroy on that thread, within the GLContext's lifetime; never for a headless
    // context. The theme is fixed; the DPI scale follows the window's monitor
    // (sync_dpi_scale()).
    class ImGuiPanelContext {
    public:
        ImGuiPanelContext(GLContext& ctx, const FigureOptions& opts);

        ~ImGuiPanelContext();

        ImGuiPanelContext(const ImGuiPanelContext&) = delete;

        ImGuiPanelContext& operator=(const ImGuiPanelContext&) = delete;

        // Makes this the current ImGui context (already per-thread via
        // sextant_imconfig.h; this makes it a frame-loop invariant).
        void make_current() const;

        // Re-scales the chrome if the window moved to a monitor with a different
        // content scale. Call once per frame before NewFrame().
        void sync_dpi_scale(const GLContext& ctx);

        // The chrome scale (1.0 = 100%): `WindowLink::chrome_scale()`, not the
        // raw content scale -- on a platform that scales the framebuffer
        // instead of the window, the scaling is already done (v1.0 step 21.6).
        float dpi_scale() const { return dpi_scale_; }

    private:
        ImGuiContext* ctx_ = nullptr;
        PanelTheme theme_ = PanelTheme::Light;
        float dpi_scale_ = 1.0f;
    };
} // namespace sextant
