// The library window as a consumer of the kit (GUI-kit R7): the backend-free
// ImGui setup any host calls, and FigureWindowShell on a hidden window -- its
// windows and dock layout, a View toggle, the dialogs' windows, and a resize
// request carried out once the plot has a size.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_link.h"
#include "window_broker.h"
#include "platform/platform.h"
#include "widgets/figure_window_shell.h"
#include <GLFW/glfw3.h>

namespace lt {
    using namespace sextant;

    namespace {
        std::vector<float> style_sample() {
            const ImGuiStyle& s = ImGui::GetStyle();
            return { s.WindowPadding.x, s.FramePadding.y, s.ItemSpacing.x, s.ScrollbarSize,
                     s.GrabMinSize, s.IndentSpacing, s.FontScaleDpi };
        }

        bool active(const char* name) {
            const ImGuiWindow* w = ImGui::FindWindowByName(name);
            return w && w->Active; // submitted in the last frame (WasActive lags one)
        }
    } // namespace

    // -------------------------------------------------------------------------
    // setup_panel_imgui() / follow_panel_scale(): no backend
    // -------------------------------------------------------------------------
    void test_panel_imgui_setup() {
        std::printf("\n[window shell: the kit's ImGui setup]\n");

        ImGuiContext* ictx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ictx);
        ImGuiIO& io = ImGui::GetIO();
        check(!io.ConfigDragClickToInputText && io.Fonts->Sources.Size == 0,
              "setup: (control) a fresh context has neither");

        setup_panel_imgui(PanelTheme::Light, 1.5f);
        check(io.ConfigDragClickToInputText, "setup: click-to-type drags on");
        // Roboto-Medium is ~160 KB of TTF; ImGui's default ProggyClean is far
        // smaller, and is not added at all.
        check(io.Fonts->Sources.Size == 1 && io.Fonts->Sources[0].FontDataSize > 100000,
              "setup: one font, the panel's Roboto, and not ImGui's default");
        check(io.Fonts->Sources.Size == 1 && io.Fonts->Sources[0].SizePixels == 13.0f,
              "setup: at the panel's base size, 13 px");
        check(ImGui::GetStyle().FontScaleDpi == 1.5f, "setup: the style at the scale given");
        check(!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable) && io.IniFilename != nullptr,
              "setup: and nothing of the shell's (docking, imgui.ini stay the host's choice)");
        const std::vector<float> at_15 = style_sample();

        // follow_panel_scale(): only a real change re-styles.
        float current = 1.5f;
        check(!follow_panel_scale(PanelTheme::Light, current, 1.5f) && current == 1.5f,
              "follow: the same scale is no change");
        check(!follow_panel_scale(PanelTheme::Light, current, 0.0f) && current == 1.5f,
              "follow: nor is an unknown (<= 0) one");
        check(follow_panel_scale(PanelTheme::Light, current, 1.0f) && current == 1.0f &&
              ImGui::GetStyle().FontScaleDpi == 1.0f,
              "follow: a new scale re-styles and becomes the current one");
        follow_panel_scale(PanelTheme::Light, current, 1.5f);
        check(style_sample() == at_15, "follow: 1.5 -> 1.0 -> 1.5 does not compound");

        ImGui::DestroyContext(ictx);
        ImGui::SetCurrentContext(nullptr);

        // A scale <= 0 sets up at 100%.
        ictx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ictx);
        setup_panel_imgui(PanelTheme::Dark, 0.0f);
        check(ImGui::GetStyle().FontScaleDpi == 1.0f, "setup: an unknown scale is 100%");
        ImGui::DestroyContext(ictx);
        ImGui::SetCurrentContext(nullptr);
    }

    // -------------------------------------------------------------------------
    // FigureWindowShell on a hidden window
    // -------------------------------------------------------------------------
    void test_figure_window_shell() {
        std::printf("\n[window shell: the library window, composed]\n");

        FigureOptions opts;
        opts.width = 800;
        opts.height = 600;
        GLContext ctx({.width = 800, .height = 600, .title = "window_shell", .visible = false});
        RenderDevice dev;
        FigureWindowShell shell(ctx, opts);

        FigureSnapshot fs = make_snapshot(1, 2, 2);
        FigureEditBox edit_box;
        FigureWindowState st;

        auto frame = [&] {
            shell.begin_frame(ctx);
            ctx.link().sync_state();
            platform::GLContextLock gl_lock;
            shell.frame(ctx, dev, fs, edit_box, st);
            ctx.link().service_requests();
        };

        // ---- The windows, under today's names, docked as today.
        for (int i = 0; i < 6; ++i) frame();
        const ImGuiWindow* plot = ImGui::FindWindowByName("Plot");
        const ImGuiWindow* cos  = ImGui::FindWindowByName("Cosmetic");
        const ImGuiWindow* data = ImGui::FindWindowByName("Data");
        check(plot && cos && data && plot->DockId && cos->DockId,
              "shell: Plot, Cosmetic and Data, all docked");
        check(plot && cos && data && cos->DockId == data->DockId && plot->DockId != cos->DockId,
              "shell: Cosmetic and Data share the side node, Plot has its own");
        check(st.plot_info.plot_w > 0 && st.plot_info.plot_w == st.plot.live_plot_w.load() &&
              st.plot_info.resolved.size() == 2,
              "shell: keeps the plot view's info, which the inspectors read");
        check(shell.view().fbo().width() > 0,
              "shell: its own plot view drew");

        // ---- View > Cosmetic off, and back.
        st.shell.cosmetic_visible = false;
        frame();
        frame();
        check(!st.shell.layout_cosmetic_visible && !active("Cosmetic") && active("Data"),
              "shell: hiding Cosmetic rebuilds the dock without it; Data stays");
        st.shell.cosmetic_visible = true;
        frame();
        frame();
        check(st.shell.layout_cosmetic_visible && active("Cosmetic"),
              "shell: and showing it brings it back");

        // ---- The dialogs open in their windows.
        st.save.open = true;
        st.resize.open = true;
        frame();
        check(active("Save Figure") && active("Resize"), "shell: the dialogs in their windows");
        st.save.open = st.resize.open = false;
        st.save.warning = "nothing was written";
        st.save.failed = true;
        frame();
        check(active("Save failed###save_warning"), "shell: and a failed save's warning");
        st.save.warning.clear();
        frame();
        check(!active("Save failed###save_warning"), "shell: gone once dismissed");

        // ---- A resize request (Figure::resize(), the Resize dialog): applied
        // once the plot has a size, so the plot area becomes what was asked.
        // Under a window manager the size arrives some events later (see
        // window_input), so pump and look again for up to a second.
        const int want_w = st.plot.live_plot_w.load() - 70;
        const int want_h = st.plot.live_plot_h.load() - 50;
        st.shell.pending_plot_w.store(want_w);
        st.shell.pending_plot_h.store(want_h);
        frame();
        check(st.shell.pending_plot_w.load() == 0 && st.shell.pending_plot_h.load() == 0,
              "shell: a pending resize is taken by the next frame");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        for (;;) {
            frame();
            if ((std::abs(st.plot.live_plot_w.load() - want_w) <= 1 &&
                 std::abs(st.plot.live_plot_h.load() - want_h) <= 1)
                || std::chrono::steady_clock::now() >= deadline)
                break;
            if (pump_runs_here()) pump_windows(0.01);
            else if constexpr (!platform::windows_on_main_thread) {
                glfwPollEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        check(std::abs(st.plot.live_plot_w.load() - want_w) <= 1 &&
              std::abs(st.plot.live_plot_h.load() - want_h) <= 1,
              "shell: and the plot gets the size asked (" +
                  std::to_string(st.plot.live_plot_w.load()) + "x" +
                  std::to_string(st.plot.live_plot_h.load()) + ", asked " +
                  std::to_string(want_w) + "x" + std::to_string(want_h) + ")");
    }
} // namespace lt
