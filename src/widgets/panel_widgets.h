#pragma once
// ImGui building blocks shared by the Cosmetic and Data panels. Conventions:
//   * Return `true` on the frame the value changed.
//   * No hardcoded pixel widths (they don't scale with DPI): use -FLT_MIN or
//     GetFontSize()-relative widths.
#include "sextant/style.h"
#include "../font_discovery.h"
#include "../plot_objects.h"
#include <imgui.h>
#include <imgui_internal.h>  // PushMultiItemsWidths() -- not stable public API
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace sextant {
    // "label | control" table with `pairs` pairs per row. The first label column
    // has a font-relative width (so tables line up); later label columns fit
    // their text; control columns share the rest. No column span: give a wide
    // control its own single-pair table.
    inline bool begin_field_table(const char* id, int pairs = 1) {
        if (!ImGui::BeginTable(id, 2 * pairs, ImGuiTableFlags_SizingFixedFit)) return false;
        for (int i = 0; i < pairs; ++i) {
            if (i == 0)
                ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::GetFontSize() * 4.5f);
            else
                ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##ctl", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        return true;
    }

    inline void end_field_table() { ImGui::EndTable(); }

    // Next pair on the current row: writes the label and sizes the control cell.
    inline void field_next(const char* label) {
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Starts a row with its first label (then field_next() for more pairs).
    inline void field_row(const char* label) {
        ImGui::TableNextRow();
        field_next(label);
    }

    // Several controls sharing one control cell evenly:
    //
    //     field_row("Gap");
    //     split_begin(2);
    //     drag_float("##gapc", ...);
    //     split_next();
    //     drag_float("##gapr", ...);
    //     split_end();
    inline void split_begin(int n) { ImGui::PushMultiItemsWidths(n, ImGui::CalcItemWidth()); }

    inline void split_next() {
        ImGui::PopItemWidth();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    inline void split_end() { ImGui::PopItemWidth(); }

    // InputText over a std::string. `buf` is re-seeded from `s` only while
    // inactive (avoids a one-frame flicker on deactivation). Returns true when
    // the text changed, with `s` updated.
    inline bool text_field(const char* id, std::string& s, char* buf, std::size_t n) {
        if (ImGui::GetActiveID() != ImGui::GetID(id))
            std::snprintf(buf, n, "%s", s.c_str());
        if (ImGui::InputText(id, buf, n)) {
            s = buf;
            return true;
        }
        return false;
    }

    // Collapsible section header. Open state doesn't persist (no ini file).
    inline bool section(const char* label, bool default_open = false) {
        return ImGui::CollapsingHeader(
            label, default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    }

    // Compact color control: swatch button plus label, RGBA entry in the popup.
    inline bool color_swatch(const char* label, Color& c) {
        return ImGui::ColorEdit4(label, &c.r,
                                 ImGuiColorEditFlags_NoInputs |
                                 ImGuiColorEditFlags_AlphaBar |
                                 ImGuiColorEditFlags_AlphaPreviewHalf);
    }

    // Drag to slide, click to type (io.ConfigDragClickToInputText).
    inline bool drag_double(const char* label, double* v, float speed,
                            const char* fmt = "%.4g") {
        return ImGui::DragScalar(label, ImGuiDataType_Double, v, speed,
                                 nullptr, nullptr, fmt);
    }

    inline bool drag_float(const char* label, float* v, float lo, float hi,
                           float speed, const char* fmt = "%.2f") {
        return ImGui::DragFloat(label, v, speed, lo, hi, fmt,
                                ImGuiSliderFlags_AlwaysClamp);
    }

    // Drag speed for an axis-limit box, derived from the visible span.
    inline float limit_drag_speed(double lo, double hi) {
        const double span = std::abs(hi - lo);
        return static_cast<float>((span > 0.0 ? span : 1.0) * 0.002);
    }

    // Font picker over discover_system_fonts(); "Default" = empty path.
    inline bool font_combo(const char* label, std::string& font_path) {
        const auto& fonts = discover_system_fonts();
        int cur = 0; // 0 == Default
        for (std::size_t i = 0; i < fonts.size(); ++i) {
            if (fonts[i].path == font_path) {
                cur = static_cast<int>(i) + 1;
                break;
            }
        }
        bool changed = false;
        if (ImGui::BeginCombo(label, cur == 0 ? "Default" : fonts[cur - 1].name.c_str())) {
            if (ImGui::Selectable("Default", cur == 0)) {
                font_path.clear();
                changed = true;
            }
            for (std::size_t i = 0; i < fonts.size(); ++i) {
                if (ImGui::Selectable(fonts[i].name.c_str(), cur == static_cast<int>(i) + 1)) {
                    font_path = fonts[i].path;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    inline bool halign_combo(const char* label, HAlign& a) {
        static const char* kNames[] = {"Left", "Center", "Right"};
        int cur = static_cast<int>(a);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            a = static_cast<HAlign>(cur);
            return true;
        }
        return false;
    }

    // Names in AxisPosition's order. The caller names Auto (Low in 2D, the
    // silhouette edge in 3D).
    inline bool axis_position_combo(const char* label, AxisPosition& p,
                                    const char* auto_name = "Auto (low)") {
        const char* names[] = {auto_name, "Low", "Mid", "High"};
        int cur = static_cast<int>(p);
        if (ImGui::Combo(label, &cur, names, IM_ARRAYSIZE(names))) {
            p = static_cast<AxisPosition>(cur);
            return true;
        }
        return false;
    }

    inline bool linestyle_combo(const char* label, LineStyle& s) {
        static const char* kNames[] = {"Solid", "Dashed", "Dotted", "Dash-dot", "None"};
        int cur = static_cast<int>(s);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            s = static_cast<LineStyle>(cur);
            return true;
        }
        return false;
    }

    // Names in MarkerStyle's order (also the shader's uMarker order).
    inline bool marker_combo(const char* label, MarkerStyle& m) {
        static const char* kNames[] = {
            "None", "Circle", "Square", "Triangle",
            "Cross", "Plus", "Diamond"
        };
        int cur = static_cast<int>(m);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            m = static_cast<MarkerStyle>(cur);
            return true;
        }
        return false;
    }

    // Names in their enums' order.
    inline bool legend_anchor_combo(const char* label, LegendAnchor& a) {
        static const char* kNames[] = {
            "Inside top-left", "Inside top-right", "Inside bottom-left", "Inside bottom-right",
            "Above, left", "Above, right", "Below, left", "Below, right",
            "Left, top", "Left, bottom", "Right, top", "Right, bottom"
        };
        int cur = static_cast<int>(a);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            a = static_cast<LegendAnchor>(cur);
            return true;
        }
        return false;
    }

    inline bool colorbar_anchor_combo(const char* label, ColorbarAnchor& a) {
        static const char* kNames[] = {"Left", "Right", "Top", "Bottom"};
        int cur = static_cast<int>(a);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            a = static_cast<ColorbarAnchor>(cur);
            return true;
        }
        return false;
    }

    // One entry per Colormap value: a new colormap only needs its name added.
    inline bool colormap_combo(const char* label, Colormap& c) {
        static const char* kNames[] = {"Viridis"};
        int cur = static_cast<int>(c);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            c = static_cast<Colormap>(cur);
            return true;
        }
        return false;
    }

    inline bool capstyle_combo(const char* label, CapStyle& s) {
        static const char* kNames[] = {"Flat", "Arrow"};
        int cur = static_cast<int>(s);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            s = static_cast<CapStyle>(cur);
            return true;
        }
        return false;
    }

    inline bool projection_combo(const char* label, Projection& p) {
        static const char* kNames[] = {"Orthographic", "Perspective"};
        int cur = static_cast<int>(p);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            p = static_cast<Projection>(cur);
            return true;
        }
        return false;
    }

    // "Axis {index} - {title}" selector in the menu bar. Width from the caller's
    // SetNextItemWidth(). Writes the chosen slot to selected_slot_index and
    // returns true; the caller applies it via select_slot().
    inline bool axes_selector(const char* label, const FigureSnapshot& fsnap,
                              int& selected_slot_index) {
        auto entry_label = [](const FigureAxesSnapshot& fa) {
            std::string s = "Axis " + std::to_string(fa.slot.index);
            if (fa.is_3d()) s += " (3D)";
            // Lists 3D cells alongside 2D ones (both have titles).
            const std::string& title = std::visit(
                [](const auto& sn) -> const std::string& { return sn.title; }, fa.snap);
            if (!title.empty()) {
                constexpr std::size_t kMaxTitle = 24;
                s += " - " + (title.size() > kMaxTitle
                                  ? title.substr(0, kMaxTitle - 1) + "\xE2\x80\xA6"
                                  : title);
            }
            return s;
        };

        std::string preview = "Axis " + std::to_string(selected_slot_index);
        for (const auto& fa: fsnap.axes)
            if (fa.slot.index == selected_slot_index) {
                preview = entry_label(fa);
                break;
            }

        bool changed = false;
        if (ImGui::BeginCombo(label, preview.c_str())) {
            for (const auto& fa: fsnap.axes) {
                const bool sel = fa.slot.index == selected_slot_index;
                if (ImGui::Selectable(entry_label(fa).c_str(), sel)) {
                    selected_slot_index = fa.slot.index;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
} // namespace sextant
