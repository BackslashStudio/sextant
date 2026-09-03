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

    switch (opts.theme) {
        case PanelTheme::Light:   ImGui::StyleColorsLight();   break;
        case PanelTheme::Classic: ImGui::StyleColorsClassic(); break;
        case PanelTheme::Dark:
        default:                  ImGui::StyleColorsDark();    break;
    }

    // DPI scale for the panel chrome, queried directly from GLFW rather than
    // io.DisplayFramebufferScale (used for the plot FBO in panel.cpp), because
    // that field is not populated until the first ImGui_ImplGlfw_NewFrame(),
    // which has not happened yet. A one-time, construction-time measurement:
    // it does not react to the window later moving to a different-DPI monitor.
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(ctx.window(), &xscale, &yscale);
    const float dpi_scale = xscale;

    // Must run after StyleColorsX() above: each of those resets ImGuiStyle
    // to its own unscaled baseline, so scaling first would be undone.
    ImGui::GetStyle().ScaleAllSizes(dpi_scale);

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

    // Bake the font at its final physical pixel size up front (crisp at any
    // DPI) rather than leaving it at a fixed size and relying on
    // io.FontGlobalScale, which is a blurry post-rasterization multiplier.
    // Roboto-Medium replaces ImGui's default embedded font (ProggyClean),
    // which is hand-tuned for ~13px and looks blocky once scaled up.
    ImFontConfig font_cfg;
    const float size_pixels = 13.0f * dpi_scale;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        panel_font::k_roboto_medium_compressed_data,
        static_cast<int>(panel_font::k_roboto_medium_compressed_size),
        size_pixels, &font_cfg);

    // Install_callbacks=true is safe here: GLContext registers only
    // glfwSetFramebufferSizeCallback (see gl_context.cpp), so ImGui's
    // default non-chaining callback install has nothing to conflict with.
    ImGui_ImplGlfw_InitForOpenGL(ctx.window(), true);
    ImGui_ImplOpenGL3_Init("#version 410"); // matches the GL 4.1 core context
}

void ImGuiPanelContext::make_current() const {
    ImGui::SetCurrentContext(ctx_);
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
