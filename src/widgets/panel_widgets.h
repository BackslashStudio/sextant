#pragma once
// Small ImGui building blocks shared by the Controls and Data panels. Header-
// only and `inline`, so both panel.cpp and data_panel.cpp can include them.
//
// Two conventions everything here follows:
//   * Return `true` on the frame the value changed, so call sites keep the
//     `if (widget(...)) push_edit();` shape.
//   * Never hardcode a pixel width. The panel style is ScaleAllSizes(dpi)'d
//     but literal floats are not, so a hardcoded 130.0f is wrong on a
//     high-DPI display and cramped in the 240px default dock column. Widths
//     are either -FLT_MIN (fill the cell) or GetFontSize()-relative.
#include "sextant/style.h"
#include "../font_discovery.h"
#include "../plot_objects.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace sextant {

// Two-column "label | control" row layout. The label column is sized from
// the font rather than a pixel constant, and the control column always fills
// what is left, which is what keeps these rows legible at the default panel
// width where ImGui's built-in right-hand labels would be clipped.
inline bool begin_field_table(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * 4.5f);
    ImGui::TableSetupColumn("##ctl", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
inline void end_field_table() { ImGui::EndTable(); }

// Starts a row and writes its label, leaving the cursor in the control cell
// with the item width already set to fill it.
inline void field_row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// Collapsible section header. Open/closed state lives in ImGui's per-window
// storage, which does not survive a show() because io.IniFilename is null --
// hence default_open for the two sections worth paying for on every startup.
inline bool section(const char* label, bool default_open = false) {
    return ImGui::CollapsingHeader(
        label, default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
}

// Compact color control: a swatch button plus the label, with the RGBA
// number entry moved into the click-through picker popup. The old full-width
// ColorEdit4 rows spent the entire panel width on four number boxes each.
inline bool color_swatch(const char* label, Color& c) {
    return ImGui::ColorEdit4(label, &c.r,
                             ImGuiColorEditFlags_NoInputs |
                             ImGuiColorEditFlags_AlphaBar |
                             ImGuiColorEditFlags_AlphaPreviewHalf);
}

// Blender-style numeric entry: drag horizontally to slide the value, click
// (without dragging) to type one. The click-to-type half comes from
// io.ConfigDragClickToInputText, set once in ImGuiPanelContext.
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

// Drag speed for an axis-limit box. A fixed step is useless across the range
// of scales a plot can hold — 0.01 is absurd on a 1e9 axis and unusably
// coarse on a 1e-6 one — so it is derived from the span currently shown.
inline float limit_drag_speed(double lo, double hi) {
    const double span = std::abs(hi - lo);
    return static_cast<float>((span > 0.0 ? span : 1.0) * 0.002);
}

// Font-family picker over discover_system_fonts(), with an explicit
// "Default" entry mapping to an empty path. Used by the axes, legend and
// colorbar, each of which carries its own font_path.
inline bool font_combo(const char* label, std::string& font_path) {
    const auto& fonts = discover_system_fonts();
    int cur = 0;  // 0 == Default
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        if (fonts[i].path == font_path) { cur = static_cast<int>(i) + 1; break; }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, cur == 0 ? "Default" : fonts[cur - 1].name.c_str())) {
        if (ImGui::Selectable("Default", cur == 0)) { font_path.clear(); changed = true; }
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
    static const char* kNames[] = { "Left", "Center", "Right" };
    int cur = static_cast<int>(a);
    if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
        a = static_cast<HAlign>(cur);
        return true;
    }
    return false;
}

inline bool linestyle_combo(const char* label, LineStyle& s) {
    static const char* kNames[] = { "Solid", "Dashed", "Dotted", "Dash-dot", "None" };
    int cur = static_cast<int>(s);
    if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
        s = static_cast<LineStyle>(cur);
        return true;
    }
    return false;
}

// "Axis {index} - {title}" selector, shared by the Controls and Data panels
// (Data needs its own copy because Controls can be hidden). Showing the title
// is what makes a 3x4 subplot grid navigable. Writes the chosen slot index
// into selected_slot_index and returns true if it changed.
inline bool axes_selector(const char* label, const FigureSnapshot& fsnap,
                          int& selected_slot_index) {
    auto entry_label = [](const FigureAxesSnapshot& fa) {
        std::string s = "Axis " + std::to_string(fa.slot.index);
        if (!fa.snap.title.empty()) {
            constexpr std::size_t kMaxTitle = 24;
            s += " - " + (fa.snap.title.size() > kMaxTitle
                              ? fa.snap.title.substr(0, kMaxTitle - 1) + "\xE2\x80\xA6"
                              : fa.snap.title);
        }
        return s;
    };

    std::string preview = "Axis " + std::to_string(selected_slot_index);
    for (const auto& fa : fsnap.axes)
        if (fa.slot.index == selected_slot_index) { preview = entry_label(fa); break; }

    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const auto& fa : fsnap.axes) {
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
