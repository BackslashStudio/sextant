#include "imgui_context.h"
#include "../renderer/gl_context.h"
// The only includer, and keep it so: the generated header's arrays are
// `static`, so each including file gets its own 115 KB copy. Others call
// setup_panel_imgui() instead (GUI-kit R7).
#include "panel_font.h"
#include "sextant/figure.h"
#include "imgui_impl_sextant.h"
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

// Storage for the thread-local context pointer declared in sextant_imconfig.h;
// must be at global scope.
thread_local ImGuiContext* MyImGuiTLS = nullptr;

namespace sextant {
    ImGuiPanelContext::ImGuiPanelContext(GLContext& ctx, const FigureOptions& opts) {
        IMGUI_CHECKVERSION();
        ctx_ = ImGui::CreateContext();

        // CreateContext() may leave another context current; select ours.
        ImGui::SetCurrentContext(ctx_);

        theme_ = opts.theme;

        // Only the part of the content scale the framebuffer is not already
        // providing -- read from the link, not from io.DisplayFramebufferScale,
        // which is unset before the first NewFrame. Re-checked every frame by
        // sync_dpi_scale().
        dpi_scale_ = ctx.link().chrome_scale();
        if (dpi_scale_ <= 0.0f) dpi_scale_ = 1.0f;
        setup_panel_imgui(theme_, dpi_scale_);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // No imgui.ini: every show() starts from the default dock split, and two
        // Figures can't collide on one file.
        io.IniFilename = nullptr;

        ImGui_ImplSextant_Init(ctx.link());
        ImGui_ImplOpenGL3_Init("#version 410"); // matches the GL 4.1 core context
    }

    void ImGuiPanelContext::make_current() const {
        ImGui::SetCurrentContext(ctx_);
    }

    void apply_panel_style(PanelTheme theme, float scale) {
        // Start from a default style so scaling doesn't compound.
        ImGuiStyle s;
        switch (theme) {
            case PanelTheme::Light: ImGui::StyleColorsLight(&s);
                break;
            case PanelTheme::Classic: ImGui::StyleColorsClassic(&s);
                break;
            case PanelTheme::Dark:
            default: ImGui::StyleColorsDark(&s);
                break;
        }
        s.ScaleAllSizes(scale); // padding, rounding, scrollbars, borders
        s.FontScaleDpi = scale; // text, re-rasterized rather than stretched
        ImGui::GetStyle() = s;
    }

    void setup_panel_imgui(PanelTheme theme, float scale) {
        ImGuiIO& io = ImGui::GetIO();

        // Plain click on a Drag widget enters text input (see drag_double()).
        io.ConfigDragClickToInputText = true;

        // Added at base size: since imgui 1.92 glyphs rasterize on demand at the
        // current FontScaleDpi, so the scale can change later. Roboto-Medium
        // replaces the default ProggyClean.
        ImFontConfig font_cfg;
        io.Fonts->AddFontFromMemoryCompressedTTF(
            panel_font::k_roboto_medium_compressed_data,
            static_cast<int>(panel_font::k_roboto_medium_compressed_size),
            13.0f, &font_cfg);

        apply_panel_style(theme, scale > 0.0f ? scale : 1.0f);
    }

    bool follow_panel_scale(PanelTheme theme, float& current, float want) {
        // Exact compare: the platform hands back the same float until the
        // monitor changes.
        if (want <= 0.0f || want == current) return false;
        current = want;
        apply_panel_style(theme, current);
        return true;
    }

    void ImGuiPanelContext::sync_dpi_scale(const GLContext& ctx) {
        follow_panel_scale(theme_, dpi_scale_, ctx.link().chrome_scale());
    }

    ImGuiPanelContext::~ImGuiPanelContext() {
        // Backend shutdowns use the current context, so select ours.
        ImGui::SetCurrentContext(ctx_);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSextant_Shutdown();
        ImGui::DestroyContext(ctx_);
        ctx_ = nullptr;
    }
} // namespace sextant
