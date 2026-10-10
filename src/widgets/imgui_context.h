#pragma once

#include "sextant/figure.h"   // PanelTheme, kept for the stored theme

struct ImGuiContext;

namespace sextant {
    class GLContext;

    // Writes the panel style for `theme` at chrome scale `scale` (1.0 = 100%).
    // Built from a default ImGuiStyle so repeated calls don't compound scaling.
    void apply_panel_style(PanelTheme theme, float scale);

    // What the kit's components need from an ImGui context, on the current one,
    // with no platform or renderer backend: the Roboto panel
    // font, click-to-type drags (drag_double() relies on it) and the panel style
    // at chrome scale `scale`. Call once, before the first frame. Any host: the
    // library window (ImGuiPanelContext) or an app on its own backend.
    void setup_panel_imgui(PanelTheme theme, float scale);

    // Re-styles for a new chrome scale when `want` (> 0) differs from `current`,
    // which it then becomes; true if it did. Exact compare: a scale read from the
    // platform is the same float every frame until the monitor changes. Call
    // before NewFrame().
    bool follow_panel_scale(PanelTheme theme, float& current, float want);

    // The library window's ImGui context: setup_panel_imgui() plus what is the
    // shell's own -- docking, no imgui.ini, sextant's platform backend over the
    // GLContext's WindowLink, and the GL3 renderer. Create and destroy on the
    // window's thread, within the GLContext's lifetime; never for a headless
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
        // instead of the window, the scaling is already done.
        float dpi_scale() const { return dpi_scale_; }

    private:
        ImGuiContext* ctx_ = nullptr;
        PanelTheme theme_ = PanelTheme::Light;
        float dpi_scale_ = 1.0f;
    };
} // namespace sextant
