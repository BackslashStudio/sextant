#pragma once

struct ImGuiContext;

namespace sextant {
class GLContext;
struct FigureOptions;

// RAII owner of exactly one Dear ImGui context bound to one GLContext's
// window. Must be constructed and destroyed on that GLContext's thread,
// strictly after it exists and strictly before it is torn down. Never
// construct one for a headless GLContext -- there is no panel there.
//
// Theme, font and HiDPI chrome scaling are derived once at construction from
// `opts` and the window's current monitor; there is no live reactivity to a
// later theme or monitor-DPI change.
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

private:
    ImGuiContext* ctx_ = nullptr;
};

} // namespace sextant
