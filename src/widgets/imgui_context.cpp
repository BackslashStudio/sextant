#include "imgui_context.h"
#include "../renderer/gl_context.h"
#include "panel_font.h"
#include "sextant/figure.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Storage for the thread-local implicit-context pointer declared by
// src/widgets/sextant_imconfig.h (pulled in through imconfig.h's IMGUI_USER_CONFIG
// hook). Must live at global scope, outside namespace sextant, to match that
// declaration.
thread_local ImGuiContext* MyImGuiTLS = nullptr;

namespace sextant {

ImGuiPanelContext::ImGuiPanelContext(GLContext& ctx, const FigureOptions& opts) {
    IMGUI_CHECKVERSION();
    ctx_ = ImGui::CreateContext();

    // CreateContext() keeps the *previously* current context current whenever
    // there was one, so never assume it left ours selected — with one Figure
    // per thread and a thread-local GImGui there is no previous context to
    // restore, but everything below (theme, font, backend init) must land on
    // ctx_ regardless of how this thread got here.
    ImGui::SetCurrentContext(ctx_);

    theme_ = opts.theme;

    // DPI scale for the panel chrome, queried directly from GLFW rather than
    // io.DisplayFramebufferScale, because that field is not populated until
    // the first ImGui_ImplGlfw_NewFrame() -- and on Windows it is (1,1) at any
    // DPI regardless, since GLFW screen coordinates there *are* device pixels.
    // glfwGetWindowContentScale is the one query that answers 1.5 on a 150%
    // display on every platform. Seeded here and re-checked every frame by
    // sync_dpi_scale(), since a window opens on whichever monitor the OS
    // chooses -- in practice the primary, which need not be the one the user
    // ends up looking at it on.
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(ctx.window(), &xscale, &yscale);
    dpi_scale_ = (xscale > 0.0f) ? xscale : 1.0f;
    apply_panel_style(theme_, dpi_scale_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // No cross-session layout persistence for now — each Figure::show()
    // starts from the same programmatic default split (see panel.cpp's
    // one-time DockBuilder setup). Avoids two Figures colliding on the
    // same imgui.ini if ever shown simultaneously; live in-session dragging
    // (what was actually asked for) works regardless of this setting.
    io.IniFilename = nullptr;

    // Blender-style numeric fields: with this set, a Drag widget
    // treats a plain click (press and release without moving) as "enter text
    // input" instead of as a zero-distance drag. Dragging still slides the
    // value. Without it, typing into a Drag needs ctrl+click, which nobody
    // discovers. See drag_double()/drag_float() in panel_widgets.h.
    io.ConfigDragClickToInputText = true;

    // Added at its *base* size, not pre-multiplied by the DPI scale. Since
    // imgui 1.92 the atlas is dynamic -- the backend advertises
    // ImGuiBackendFlags_RendererHasTextures and glyphs are rasterized on
    // demand at style.FontSizeBase * FontScaleMain * FontScaleDpi -- so
    // apply_panel_style()'s FontScaleDpi gives a crisp rasterization at whatever
    // scale the current monitor wants, and can change it again when the window
    // moves. Baking 13 * dpi at construction was the pre-1.92 way to get
    // sharp text, and it is exactly what made the scale unchangeable
    // afterwards. Roboto-Medium replaces imgui's default embedded font
    // (ProggyClean), which is hand-tuned for ~13px and looks blocky scaled up.
    ImFontConfig font_cfg;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        panel_font::k_roboto_medium_compressed_data,
        static_cast<int>(panel_font::k_roboto_medium_compressed_size),
        13.0f, &font_cfg);

    // Install_callbacks=true is safe here: GLContext registers only
    // glfwSetFramebufferSizeCallback (see gl_context.cpp), so ImGui's
    // default non-chaining callback install has nothing to conflict with.
    ImGui_ImplGlfw_InitForOpenGL(ctx.window(), true);
    ImGui_ImplOpenGL3_Init("#version 410"); // matches the GL 4.1 core context
}

void ImGuiPanelContext::make_current() const {
    ImGui::SetCurrentContext(ctx_);
}

void apply_panel_style(PanelTheme theme, float scale) {
    // A fresh ImGuiStyle is imgui's own unscaled baseline. Rebuilding from it
    // is what makes this idempotent: ScaleAllSizes() multiplies the style it
    // is called on, and StyleColorsX() writes only Colors[], so re-theming the
    // live style and scaling it again would compound 1.5 into 2.25.
    ImGuiStyle s;
    switch (theme) {
        case PanelTheme::Light:   ImGui::StyleColorsLight(&s);   break;
        case PanelTheme::Classic: ImGui::StyleColorsClassic(&s); break;
        case PanelTheme::Dark:
        default:                  ImGui::StyleColorsDark(&s);    break;
    }
    s.ScaleAllSizes(scale);   // padding, rounding, scrollbars, borders
    s.FontScaleDpi = scale;   // text, re-rasterized rather than stretched
    ImGui::GetStyle() = s;
}

void ImGuiPanelContext::sync_dpi_scale(const GLContext& ctx) {
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(ctx.window(), &xscale, &yscale);
    if (xscale <= 0.0f) return;

    // Exact compare on purpose. Content scale is a value the platform hands
    // back unchanged (1.0, 1.25, 1.5, 2.0), not something computed here, so it
    // is either the same float as last frame or a different monitor's.
    if (xscale == dpi_scale_) return;

    dpi_scale_ = xscale;
    apply_panel_style(theme_, dpi_scale_);
}

ImGuiPanelContext::~ImGuiPanelContext() {
    // Both backend shutdowns reach their state through the current context, so
    // select ours first rather than tearing down whatever happens to be
    // current — the mirror image of the constructor's SetCurrentContext.
    ImGui::SetCurrentContext(ctx_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(ctx_);
    ctx_ = nullptr;
}

} // namespace sextant
