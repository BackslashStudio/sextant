#pragma once
// Small ImGui building blocks shared by the Cosmetic and Data panels. Header-
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
#include <imgui_internal.h>  // PushMultiItemsWidths() -- not stable public API
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace sextant {

// "label | control" row layout, `pairs` of them per row. The first label
// column is sized from the font rather than a pixel constant, and the control
// columns always share what is left, which is what keeps these rows legible at
// the default panel width where ImGui's built-in right-hand labels would be
// clipped.
//
// With more than one pair, the *first* label column keeps that same fixed
// width -- so a multi-pair table lines up with the single-pair tables above
// and below it -- while each later label column fits its own text, and the
// control columns stretch equally. A row need not use every pair; the cells it
// leaves are simply empty. ImGui tables have no column span, so a row that
// wants one wide control (a text field, a combo) belongs in a single-pair
// table of its own rather than in the first pair of a wider one.
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

// The next pair on the current row: writes its label, leaving the cursor in
// its control cell with the item width already set to fill it.
inline void field_next(const char* label) {
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// Starts a row and writes its first label, leaving the cursor in the control
// cell with the item width already set to fill it. Further pairs on the same
// row (in a table begun with pairs > 1) follow with field_next().
inline void field_row(const char* label) {
    ImGui::TableNextRow();
    field_next(label);
}

// Several controls sharing one control cell evenly -- for values that are one
// quantity in several parts (a limit's low and high, the four margins), where
// a label each would say nothing the row's label does not:
//
//     field_row("Gap");
//     split_begin(2);
//     drag_float("##gapc", ...);
//     split_next();
//     drag_float("##gapr", ...);
//     split_end();
//
// This is what ImGui's own DragFloat3 does inside. PushMultiItemsWidths()
// also cancels the width field_row() set for the next item, which would
// otherwise hand the whole cell to the first control.
inline void split_begin(int n) { ImGui::PushMultiItemsWidths(n, ImGui::CalcItemWidth()); }
inline void split_next() {
    ImGui::PopItemWidth();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}
inline void split_end() { ImGui::PopItemWidth(); }


// An InputText over a std::string the caller does not own a buffer for.
//
// `buf` is seeded from `s` only while the widget is not the active item. What
// that is worth is narrower than it looks, and the narrower claim is the true
// one: ImGui keeps its own copy of the text while an InputText is active and
// does not re-read `buf`, so seeding unconditionally would *not* eat
// keystrokes -- verified by injecting exactly that and watching the checks
// pass. What the guard prevents is a one-frame flicker on deactivation, since
// the published value lags a frame behind the edit that changed it, so a
// field clicked away from would show its old text for one frame.
//
// Returns true on the frame the text changed, with `s` updated -- so a caller
// can republish the whole options struct exactly as a checkbox does.
inline bool text_field(const char* id, std::string& s, char* buf, std::size_t n) {
    if (ImGui::GetActiveID() != ImGui::GetID(id))
        std::snprintf(buf, n, "%s", s.c_str());
    if (ImGui::InputText(id, buf, n)) { s = buf; return true; }
    return false;
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

// In AxisPosition's own order, so a name and a placement cannot drift apart
// by an index. Only the first name is the caller's, because Auto is the one
// enumerator whose meaning depends on the dimension: it resolves to Low in 2D
// and to the camera's silhouette edge in 3D, and a panel that spelled both
// "Auto" would be hiding the only interesting thing about it.
inline bool axis_position_combo(const char* label, AxisPosition& p,
                                const char* auto_name = "Auto (low)") {
    const char* names[] = { auto_name, "Low", "Mid", "High" };
    int cur = static_cast<int>(p);
    if (ImGui::Combo(label, &cur, names, IM_ARRAYSIZE(names))) {
        p = static_cast<AxisPosition>(cur);
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


// The names are in MarkerStyle's own order, `None` first as the enum has it --
// which is also the order the shader's uMarker branches are written in, so a
// name and a drawn shape cannot drift apart by an index.
inline bool marker_combo(const char* label, MarkerStyle& m) {
    static const char* kNames[] = { "None", "Circle", "Square", "Triangle",
                                    "Cross", "Plus", "Diamond" };
    int cur = static_cast<int>(m);
    if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
        m = static_cast<MarkerStyle>(cur);
        return true;
    }
    return false;
}

// Both in their enum's own order, so a name and an anchor cannot drift apart
// by an index.
inline bool legend_anchor_combo(const char* label, LegendAnchor& a) {
    static const char* kNames[] = {
        "Inside top-left", "Inside top-right", "Inside bottom-left", "Inside bottom-right",
        "Above, left",     "Above, right",     "Below, left",        "Below, right",
        "Left, top",       "Left, bottom",     "Right, top",         "Right, bottom" };
    int cur = static_cast<int>(a);
    if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
        a = static_cast<LegendAnchor>(cur);
        return true;
    }
    return false;
}

inline bool colorbar_anchor_combo(const char* label, ColorbarAnchor& a) {
    static const char* kNames[] = { "Left", "Right", "Top", "Bottom" };
    int cur = static_cast<int>(a);
    if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
        a = static_cast<ColorbarAnchor>(cur);
        return true;
    }
    return false;
}

inline bool projection_combo(const char* label, Projection& p) {
    static const char* kNames[] = { "Orthographic", "Perspective" };
    int cur = static_cast<int>(p);
    if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
        p = static_cast<Projection>(cur);
        return true;
    }
    return false;
}

// "Axis {index} - {title}" selector, drawn in the menu bar (step 10.2; it used
// to be in both the Cosmetic and Data panels). Showing the title is what makes
// a 3x4 subplot grid navigable. Takes the width set by the caller's
// SetNextItemWidth(). Writes the chosen slot index into selected_slot_index
// and returns true if one was picked -- the caller routes it through
// select_slot(), which also re-seeds the per-slot panel state.
inline bool axes_selector(const char* label, const FigureSnapshot& fsnap,
                          int& selected_slot_index) {
    auto entry_label = [](const FigureAxesSnapshot& fa) {
        std::string s = "Axis " + std::to_string(fa.slot.index);
        if (fa.is_3d()) s += " (3D)";
        // Both kinds have a title, and it is one of the very few things they
        // genuinely share -- so the selector lists 3D cells alongside 2D ones
        // rather than hiding them, whatever the sections below can edit.
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
    for (const auto& fa : fsnap.axes)
        if (fa.slot.index == selected_slot_index) { preview = entry_label(fa); break; }

    bool changed = false;
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
