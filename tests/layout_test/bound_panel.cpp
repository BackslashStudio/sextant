// The Data panel's bound objects (DataPanelState::bound): a cell edit comes back
// from draw_data_panel() as a BoundCellEdit and is not applied; the point
// insert/remove controls do nothing; an unbound object edits as before. Driven
// headless through ImGui, as the panel tests in bar3d.cpp. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "widgets/data_panel.h"
#include "widgets/panel.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdlib>
#include <string>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr ObjectId kId = 77;

        struct Rig {
            FigureSnapshot fs;
            FigureWindowState st;
            FigureEditBox box;
            ImGuiContext* ctx = nullptr;
            std::vector<BoundCellEdit> requests;

            explicit Rig(bool bound) {
                fs = one_line_snapshot({0.0, 1.0, 2.0}, {10.0, 11.0, 12.0});
                fs.axes[0].snap2d()->lines[0].id = kId;
                fs.axes[0].snap2d()->lines[0].opts.color = Color::Blue;
                if (bound) st.data.bound = {kId};
                ctx = ImGui::CreateContext();
                ImGui::SetCurrentContext(ctx);
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(560.0f, 900.0f);
                io.DeltaTime = 1.0f / 60.0f;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.Fonts->AddFontDefault();
                for (int i = 0; i < 3; ++i) frame();
            }

            ~Rig() { ImGui::DestroyContext(ctx); }

            void frame() {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(520.0f, 860.0f));
                FigureContext c = panel_ctx(fs, box, st);
                ImGui::Begin("Data", nullptr, ImGuiWindowFlags_NoCollapse);
                push_figure_id(c.figure_id);
                auto r = draw_data_panel(c, st.data);
                requests.insert(requests.end(), r.begin(), r.end());
                ImGui::PopID();
                ImGui::End();
                ImGui::Render();
            }

            void click(float x, float y) {
                ImGuiIO& io = ImGui::GetIO();
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = true;
                frame();
                io.MouseDown[0] = false;
                frame();
            }
        };

        // How many plot ops a sweep of clicks over the table area journals (only
        // the +/- buttons emit without typing).
        int sweep_ops(Rig& rig) {
            int ops = 0;
            for (float y = 60.0f; y < 840.0f; y += 5.0f)
                for (float x = 8.0f; x < 140.0f; x += 5.0f) {
                    rig.click(x, y);
                    if (ImGui::GetActiveID() != 0) {   // a field took focus: let it go
                        ImGui::ClearActiveID();
                        rig.frame();
                    }
                    if (auto e = rig.box.load_and_clear())
                        for (const auto& [i, a]: e->per_axes) ops += static_cast<int>(a.plot_ops.size());
                }
            return ops;
        }

        // Clicks until a numeric cell is being edited, types `text`, presses Enter.
        bool type_into_a_cell(Rig& rig, const char* text) {
            for (float y = 60.0f; y < 840.0f; y += 6.0f)
                for (float x = 100.0f; x < 500.0f; x += 12.0f) {
                    rig.click(x, y);
                    const ImGuiID id = ImGui::GetActiveID();
                    const ImGuiInputTextState* ts = id ? ImGui::GetInputTextState(id) : nullptr;
                    // A numeric cell (InputDouble) holding one of the plot's values,
                    // not the name field or a style field.
                    bool cell = false;
                    if (ts && ts->TextLen > 0) {
                        const std::string txt(ts->TextA.Data, static_cast<std::size_t>(ts->TextLen));
                        char* end = nullptr;
                        const double v = std::strtod(txt.c_str(), &end);
                        cell = end && *end == '\0' && (v == 0.0 || v == 1.0 || v == 2.0 || v == 10.0 ||
                                                        v == 11.0 || v == 12.0);
                    }
                    if (!cell) {
                        if (id) {
                            ImGui::ClearActiveID();
                            rig.frame();
                        }
                        continue;
                    }
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    io.AddKeyEvent(ImGuiKey_A, true);
                    rig.frame();
                    io.AddKeyEvent(ImGuiKey_A, false);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    rig.frame();
                    io.AddInputCharactersUTF8(text);
                    rig.frame();
                    io.AddKeyEvent(ImGuiKey_Enter, true);
                    rig.frame();
                    io.AddKeyEvent(ImGuiKey_Enter, false);
                    rig.frame();
                    return true;
                }
            return false;
        }
    } // namespace

    void test_bound_panel() {
        std::printf("\n[data panel: bound objects]\n");

        {
            Rig free(false);
            check(sweep_ops(free) > 0, "unbound: the point +/- buttons journal row edits");
        }
        {
            Rig bound(true);
            check(sweep_ops(bound) == 0 && bound.requests.empty(),
                  "bound: the point +/- buttons are disabled -- nothing journaled, nothing requested");
        }
        {
            Rig bound(true);
            const bool typed = type_into_a_cell(bound, "42");
            const auto e = bound.box.load_and_clear();
            int ops = 0;
            if (e) for (const auto& [i, a]: e->per_axes) ops += static_cast<int>(a.plot_ops.size());
            check(typed, "bound: a cell took the keyboard");
            check(bound.requests.size() == 1 && ops == 0,
                  "bound: a cell edit is returned as a request, not journaled (" +
                      std::to_string(bound.requests.size()) + " requests, " + std::to_string(ops) + " ops)");
            if (bound.requests.size() == 1) {
                const BoundCellEdit& r = bound.requests[0];
                check(r.id == kId && r.kind == PlotKind::Line && r.value == 42.0 && r.element < 3 &&
                          (r.column == 0 || r.column == 1) && r.plane_index == -1,
                      "bound: the request names the object, the cell and the value");
            }
        }
        {
            Rig free(false);
            type_into_a_cell(free, "42");
            const auto e = free.box.load_and_clear();
            int ops = 0;
            if (e) for (const auto& [i, a]: e->per_axes) ops += static_cast<int>(a.plot_ops.size());
            check(free.requests.empty() && ops == 1, "unbound: the same edit is journaled, nothing requested");
        }
    }
} // namespace lt
