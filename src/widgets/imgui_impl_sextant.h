#pragma once

namespace sextant {
    class WindowLink;

    // sextant's ImGui platform backend, in place of imgui_impl_glfw. That one
    // calls GLFW's window, cursor and clipboard functions every frame from
    // whichever thread renders, and on macOS those belong to the main thread; this
    // one reads the window's state from a WindowLink mirror and replays the input
    // its queue carries. The render backend is still ImGui_ImplOpenGL3.
    //
    // One backend per ImGui context, all three called on the thread that owns it.

    void ImGui_ImplSextant_Init(WindowLink& link);

    // Display size, time step, input, and the cursor shape ImGui asked for last
    // frame. Before ImGui::NewFrame(), as imgui_impl_glfw's NewFrame is.
    void ImGui_ImplSextant_NewFrame(WindowLink& link);

    void ImGui_ImplSextant_Shutdown();
} // namespace sextant
