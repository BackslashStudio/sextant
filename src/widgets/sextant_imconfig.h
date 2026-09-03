#pragma once

// Dear ImGui build-time configuration, injected via the IMGUI_USER_CONFIG
// compile definition rather than by editing third_party/imgui/imconfig.h,
// which is vendored verbatim and would lose local edits on the next setup run.
//
// Every Figure::show() runs its own window thread with its own ImGuiContext.
// ImGui's implicit context pointer is a process-wide global by default, so N
// such threads fight over one pointer: with two Figures open, the second
// thread's CreateContext() would hand its whole setup to the *first* thread's
// live context, because CreateContext() restores the previously current one.
// Giving the pointer thread-local storage is ImGui's own documented remedy
// for the N-threads/N-contexts case. MyImGuiTLS is defined in
// src/widgets/imgui_context.cpp.
struct ImGuiContext;
extern thread_local ImGuiContext* MyImGuiTLS;
#define GImGui MyImGuiTLS

// Point imgui_impl_opengl3.cpp at the GLAD loader sextant already uses instead
// of the gl3w-derived one it bundles. IMGUI_IMPL_OPENGL_LOADER_CUSTOM (set by
// CMakeLists.txt) suppresses the bundled loader; this include supplies the GL
// entry points in its place.
//
// Not merely de-duplication: the bundled loader keeps its function-pointer
// table in an un-refcounted process-wide global that
// ImGui_ImplOpenGL3_Shutdown() zeroes, so with one context per window thread,
// closing any one window wiped the GL entry points out from under every other
// window still rendering. The custom loader compiles both InitLoader() and
// ShutdownLoader() down to no-ops.
//
// glad.h is self-contained, so it is safe to inject into every translation
// unit that sees imgui.h.
#include <glad/glad.h>
