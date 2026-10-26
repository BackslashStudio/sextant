#pragma once
// ImGui building blocks shared by the Cosmetic and Data panels. Conventions:
//   * Return `true` on the frame the value changed.
//   * No hardcoded pixel widths (they don't scale with DPI): use -FLT_MIN or
//     GetFontSize()-relative widths.
#include "sextant/style.h"
#include "../colormaps.h"
#include "../font_discovery.h"
#include "../plot_objects.h"
#include <imgui.h>
#include <imgui_internal.h>  // PushMultiItemsWidths() -- not stable public API
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

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

    // An override of a shared value (AxesStyle::xtick_color over tick_color):
    // shown dimmed at the shared value while unset, set by an edit, cleared
    // from its right-click menu. Call right after the item; true = cleared.
    // `follows` names the shared value ("Both").
    inline bool override_menu(const char* label, bool set, const char* follows) {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(set ? "Its own. Right-click to follow %s." : "Follows %s until edited.", follows);
        // Named after the widget: the last item after a ColorEdit4 is its
        // group, whose id other swatches share (one popup for all of them).
        const std::string id = std::string(label) + "##follow";
        if (!set || !ImGui::BeginPopupContextItem(id.c_str())) return false;
        const std::string item = std::string("Follow ") + follows;
        const bool clear = ImGui::MenuItem(item.c_str());
        ImGui::EndPopup();
        return clear;
    }

    inline bool override_color(const char* label, std::optional<Color>& v, const Color& shared,
                               const char* follows = "Both") {
        Color c = v.value_or(shared);
        const bool set = v.has_value();
        if (!set) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
        bool changed = ImGui::ColorEdit4(label, &c.r,
                                         ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar |
                                         ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                                         ImGuiColorEditFlags_NoTooltip);
        if (!set) ImGui::PopStyleVar();
        if (changed) v = c;
        if (override_menu(label, set, follows)) {
            v.reset();
            changed = true;
        }
        return changed;
    }

    inline bool override_float(const char* label, std::optional<float>& v, float shared, float lo, float hi,
                               float speed, const char* fmt, const char* follows = "Both") {
        float f = v.value_or(shared);
        const bool set = v.has_value();
        if (!set) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
        bool changed = ImGui::DragFloat(label, &f, speed, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
        if (!set) ImGui::PopStyleVar();
        if (changed) v = f;
        if (override_menu(label, set, follows)) {
            v.reset();
            changed = true;
        }
        return changed;
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

    // Names in Scale's order.
    inline bool scale_combo(const char* label, Scale& s) {
        static const char* kNames[] = {"Linear", "Log", "Symlog"};
        int cur = static_cast<int>(s);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            s = static_cast<Scale>(cur);
            return true;
        }
        return false;
    }

    // Names in Adjustable's order; Datalim greyed when `datalim_ok` is false
    // (both axes shared, see Axes::set_aspect()).
    inline bool adjustable_combo(const char* label, Adjustable& a, bool datalim_ok) {
        static const char* kNames[] = {"Box", "Datalim"};
        bool changed = false;
        if (ImGui::BeginCombo(label, kNames[static_cast<int>(a)])) {
            for (int k = 0; k < IM_ARRAYSIZE(kNames); ++k) {
                const bool off = k == static_cast<int>(Adjustable::Datalim) && !datalim_ok;
                if (ImGui::Selectable(kNames[k], static_cast<int>(a) == k,
                                      off ? ImGuiSelectableFlags_Disabled : 0)) {
                    a = static_cast<Adjustable>(k);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // An optional<bool> as Auto / On / Off.
    inline bool auto_on_off_combo(const char* label, std::optional<bool>& v) {
        static const char* kNames[] = {"Auto", "On", "Off"};
        int cur = !v ? 0 : (*v ? 1 : 2);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            v = cur == 0 ? std::nullopt : std::optional<bool>(cur == 1);
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

    inline bool drawstyle_combo(const char* label, DrawStyle& s) {
        static const char* kNames[] = {"Straight", "Steps (pre)", "Steps (mid)", "Steps (post)"};
        int cur = static_cast<int>(s);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            s = static_cast<DrawStyle>(cur);
            return true;
        }
        return false;
    }

    inline bool orientation_combo(const char* label, Orientation& o) {
        static const char* kNames[] = {"Vertical", "Horizontal"};
        int cur = static_cast<int>(o);
        if (ImGui::Combo(label, &cur, kNames, IM_ARRAYSIZE(kNames))) {
            o = static_cast<Orientation>(cur);
            return true;
        }
        return false;
    }

    // Names in MarkerStyle's order (also the shader's uMarker order).
    inline bool marker_combo(const char* label, MarkerStyle& m) {
        static const char* kNames[] = {
            "None", "Circle", "Square", "Triangle",
            "Cross", "Plus", "Diamond", "Triangle down", "Triangle left",
            "Triangle right", "Star", "Pentagon", "Hexagon", "Thin cross (x)",
            "Thin plus (+)", "Vertical line (|)", "Horizontal line (_)"
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
    // Every map by name: the built-ins under Sequential, Diverging and Cyclic,
    // then registered ones, with a "Reversed" toggle at the top of the list (the
    // popup stays open for it).
    inline bool colormap_combo(const char* label, Colormap& c) {
        bool changed = false;
        const std::string preview = c.name();
        if (ImGui::BeginCombo(label, preview.c_str())) {
            bool rev = c.is_reversed();
            if (ImGui::Checkbox("Reversed", &rev)) {
                c = c.reversed();
                changed = true;
            }
            const std::vector<std::string> names = Colormap::names();
            auto item = [&](std::size_t i) {
                const Colormap m = Colormap::from_name(names[i]);
                const bool selected = m.id() == c.id();
                if (ImGui::Selectable(names[i].c_str(), selected) && !selected) {
                    c = c.is_reversed() ? m.reversed() : m;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            };
            using colormaps::Category;
            const std::size_t builtins = colormaps::builtin_count();
            for (const auto& [cat, title] : {std::pair{Category::Sequential, "Sequential"},
                                             std::pair{Category::Diverging, "Diverging"},
                                             std::pair{Category::Cyclic, "Cyclic"}}) {
                ImGui::SeparatorText(title);
                for (std::size_t i = 0; i < builtins; ++i)
                    if (colormaps::category(Colormap::from_name(names[i])) == cat) item(i);
            }
            if (names.size() > builtins) {
                ImGui::SeparatorText("Registered");
                for (std::size_t i = builtins; i < names.size(); ++i) item(i);
            }
            ImGui::EndCombo();
        }
        return changed;
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
            const std::string& title = visit_cell(
                fa, [](const auto& sn) -> const std::string& { return sn.title; });
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

    // A twinned cell's layer selector (Axes::twinx()) in the menu bar, beside
    // axes_selector(): "Host", "Twin x", "Twin y" for the layers `fa` has.
    // Width from the caller's SetNextItemWidth(). Writes the chosen layer and
    // returns true; the caller applies it via Selection::select_layer().
    inline bool layer_selector(const char* label, const FigureAxesSnapshot& fa, int& layer) {
        const auto name = [](int l) {
            return l == kLayerTwinX ? "Twin x" : l == kLayerTwinY ? "Twin y" : "Host";
        };
        bool changed = false;
        if (ImGui::BeginCombo(label, name(layer))) {
            const auto item = [&](int l) {
                if (ImGui::Selectable(name(l), layer == l)) {
                    layer = l;
                    changed = true;
                }
            };
            item(0);
            for (const TwinSnapshot& t: fa.twins) item(t.layer);
            ImGui::EndCombo();
        }
        return changed;
    }
} // namespace sextant
