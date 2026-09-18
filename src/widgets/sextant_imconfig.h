#pragma once

// Dear ImGui build config, injected via IMGUI_USER_CONFIG (the vendored
// imconfig.h stays untouched).
//
// Each Figure::show() has its own window thread and ImGuiContext, so the
// implicit context pointer is made thread-local (ImGui's documented remedy).
// MyImGuiTLS is defined in imgui_context.cpp.
struct ImGuiContext;
extern thread_local ImGuiContext* MyImGuiTLS;
#define GImGui MyImGuiTLS

// Use GLAD for imgui_impl_opengl3 (IMGUI_IMPL_OPENGL_LOADER_CUSTOM, set in
// CMakeLists.txt, disables the bundled loader). The bundled loader's shared
// global table is zeroed by each ImGui_ImplOpenGL3_Shutdown(), which broke
// other windows still rendering.
#include <glad/glad.h>
