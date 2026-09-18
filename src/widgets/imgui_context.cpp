#include "imgui_context.h"
#include "../renderer/gl_context.h"
#include "panel_font.h"
#include "sextant/figure.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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

        // Chrome DPI scale from glfwGetWindowContentScale (io.DisplayFramebufferScale
        // is unset before the first NewFrame and 1 on Windows). Re-checked every
        // frame by sync_dpi_scale().
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(ctx.window(), &xscale, &yscale);
        dpi_scale_ = (xscale > 0.0f) ? xscale : 1.0f;
        apply_panel_style(theme_, dpi_scale_);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // No imgui.ini: every show() starts from the default dock split, and two
        // Figures can't collide on one file.
        io.IniFilename = nullptr;

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

        // Safe to install callbacks: GLContext only registers the framebuffer
        // size callback.
        ImGui_ImplGlfw_InitForOpenGL(ctx.window(), true);
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

    void ImGuiPanelContext::sync_dpi_scale(const GLContext& ctx) {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(ctx.window(), &xscale, &yscale);
        if (xscale <= 0.0f) return;

        // Exact compare: content scale comes from the platform unchanged.
        if (xscale == dpi_scale_) return;

        dpi_scale_ = xscale;
        apply_panel_style(theme_, dpi_scale_);
    }

    ImGuiPanelContext::~ImGuiPanelContext() {
        // Backend shutdowns use the current context, so select ours.
        ImGui::SetCurrentContext(ctx_);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(ctx_);
        ctx_ = nullptr;
    }
} // namespace sextant
