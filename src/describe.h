#pragma once
// Field-by-field description of the public option structs (GUI-kit R11), for
// code that handles their fields by name: a project file, graph templates,
// keyword arguments, tests.
//
//     describe(v, opts);   // calls v("color", opts.color), v("linewidth", ...)
//
// One describe() per struct, taking the struct or its const, so one visitor
// can read and another write. Fields come in declaration order under their
// member names. A nested struct (`errorbar`, `margins`, `Camera3D::target`)
// is handed over as a field; a visitor that wants its fields recurses when
// `described<F>` holds, or uses for_each_leaf(), which does that for it.
//
// Leaf types: bool, int, float, double, std::size_t, std::string, Color,
// std::optional<Color>, std::optional<double>, std::vector<double>,
// std::vector<std::string>, and the enums below, whose names enum_name()
// and enum_from_name() give.
//
// Every field of a struct must be listed: a field added to a public header
// is an edit here. layout_test checks each list against the struct's layout,
// which catches a missing field unless it fits in existing padding (a bool
// beside a bool).
#include "sextant/axes3d.h"
#include "sextant/figure.h"
#include "sextant/style.h"

#include <concepts>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace sextant {

#define SEXTANT_DESCRIBE(T, ...)                                              \
    template <class V, class O>                                               \
        requires std::same_as<std::remove_const_t<O>, T>                      \
    void describe(V& v, O& o) { __VA_ARGS__ }
#define F(f) v(#f, o.f);

    SEXTANT_DESCRIBE(Vec3, F(x) F(y) F(z))
    SEXTANT_DESCRIBE(BoxAspect, F(x) F(y) F(z))

    // style.h
    SEXTANT_DESCRIBE(ErrorBarOptions,
                     F(color) F(linewidth) F(capsize) F(capstyle) F(boxwidth) F(box_alpha))
    SEXTANT_DESCRIBE(LineOptions,
                     F(color) F(linewidth) F(linestyle) F(name) F(show_legend) F(alpha) F(loop)
                     F(errorbar) F(hint_labels))
    SEXTANT_DESCRIBE(ScatterOptions,
                     F(color) F(size) F(marker) F(name) F(show_legend) F(alpha) F(edgecolor)
                     F(edge_alpha) F(edge_linewidth) F(errorbar) F(hint_labels))
    SEXTANT_DESCRIBE(ScatterZOptions,
                     F(cmap) F(size) F(marker) F(alpha) F(edgecolor) F(edge_alpha) F(edge_linewidth)
                     F(vmin) F(vmax) F(colorbar) F(name)
                     F(show_legend) F(errorbar) F(hint_labels))
    SEXTANT_DESCRIBE(BarOptions,
                     F(color) F(width) F(alpha) F(name) F(show_legend) F(edgecolor) F(linewidth)
                     F(errorbar) F(hint_labels))
    SEXTANT_DESCRIBE(HistOptions, F(density) F(cumulative))
    SEXTANT_DESCRIBE(HeatmapOptions,
                     F(cmap) F(vmin) F(vmax) F(colorbar) F(name) F(origin) F(contours)
                     F(contour_color) F(contour_linewidth) F(contour_labels) F(contour_fontsize)
                     F(hint_labels))
    SEXTANT_DESCRIBE(GridOptions, F(color) F(linestyle) F(linewidth))
    SEXTANT_DESCRIBE(AxesStyle,
                     F(background) F(spine_color) F(spine_linewidth) F(spine_bottom) F(spine_left) F(spine_top)
                     F(spine_right) F(xaxis_y) F(xaxis_z) F(yaxis_x) F(yaxis_z) F(zaxis_x)
                     F(zaxis_y) F(origin_x) F(origin_y) F(origin_z) F(frame_margin)
                     F(show_xticks) F(show_yticks) F(show_zticks)
                     F(tick_color) F(tick_length) F(tick_linewidth) F(label_color) F(label_fontsize)
                     F(title_color) F(title_fontsize) F(xtitle_color) F(xtitle_fontsize)
                     F(ytitle_color) F(ytitle_fontsize) F(ztitle_color) F(ztitle_fontsize)
                     F(font_path))
    SEXTANT_DESCRIBE(LegendOptions,
                     F(anchor) F(margin) F(offset_x) F(offset_y) F(fontsize) F(frameon)
                     F(text_color) F(frame_color) F(border_color) F(border_linewidth)
                     F(font_path))
    SEXTANT_DESCRIBE(ColorbarOptions,
                     F(anchor) F(width) F(margin) F(offset_x) F(offset_y) F(fontsize)
                     F(text_color) F(border_color) F(border_linewidth) F(font_path))
    SEXTANT_DESCRIBE(SuptitleOptions,
                     F(fontsize) F(color) F(font_path) F(align) F(offset_x) F(offset_y))
    SEXTANT_DESCRIBE(FigureMargins, F(left) F(right) F(top) F(bottom))
    SEXTANT_DESCRIBE(TextOptions,
                     F(fontsize) F(color) F(alpha) F(font_path) F(ha) F(va) F(rotation) F(dx) F(dy)
                     F(linespacing) F(background) F(edgecolor) F(edge_linewidth) F(pad)
                     F(clip_to_frame))
    SEXTANT_DESCRIBE(ArrowOptions,
                     F(head) F(tail) F(head_length) F(head_width) F(linewidth) F(color) F(linestyle)
                     F(gap_text) F(gap_point) F(arc))

    // figure.h
    SEXTANT_DESCRIBE(FigureOptions,
                     F(width) F(height) F(title) F(resizable) F(dpi) F(subplot_col_gap)
                     F(subplot_row_gap) F(margins) F(background) F(panel_width) F(supersample) F(vsync)
                     F(theme))
    SEXTANT_DESCRIBE(SvgExportOptions, F(max_splits) F(max_tests))
    SEXTANT_DESCRIBE(PngExportOptions, F(peel_layers) F(dpi))

    // axes3d.h
    SEXTANT_DESCRIBE(Camera3D,
                     F(azimuth) F(elevation) F(target) F(zoom) F(projection) F(fov))
    SEXTANT_DESCRIBE(Plane2DOptions, F(alpha) F(visible))
    SEXTANT_DESCRIBE(Bar3DOptions,
                     F(color) F(alpha) F(width) F(depth) F(bottom) F(shading) F(edges)
                     F(edgecolor) F(edge_alpha) F(edge_linewidth) F(name) F(show_legend)
                     F(hint_labels))
    SEXTANT_DESCRIBE(SurfaceOptions,
                     F(color) F(colormap) F(cmap) F(vmin) F(vmax) F(colorbar) F(name)
                     F(show_legend) F(alpha) F(shading) F(edges) F(edgecolor) F(edge_alpha)
                     F(edge_linewidth) F(hint_labels))
    SEXTANT_DESCRIBE(SurfaceTriOptions,
                     F(color) F(cmap) F(vmin) F(vmax) F(colorbar) F(name) F(show_legend) F(alpha)
                     F(shading) F(edges) F(edgecolor) F(edge_alpha) F(edge_linewidth)
                     F(hint_labels))
    SEXTANT_DESCRIBE(ErrorBar3DOptions,
                     F(color) F(linewidth) F(capsize) F(capstyle) F(boxwidth) F(box_alpha)
                     F(edge_alpha))
    SEXTANT_DESCRIBE(Scatter3DOptions,
                     F(color) F(size) F(marker) F(alpha) F(edgecolor) F(edge_alpha) F(edge_linewidth)
                     F(depthshade) F(cmap) F(vmin) F(vmax)
                     F(colorbar) F(name) F(show_legend) F(errorbar) F(hint_labels))
    SEXTANT_DESCRIBE(Line3DOptions,
                     F(color) F(linewidth) F(alpha) F(loop) F(depthshade) F(cmap) F(vmin) F(vmax)
                     F(colorbar) F(name) F(show_legend) F(errorbar) F(hint_labels))
    SEXTANT_DESCRIBE(Box3DStyle, F(panes) F(pane_color) F(pane_edge_color) F(margin))

#undef F
#undef SEXTANT_DESCRIBE

    namespace describe_detail {
        struct NoVisit {
            template <class X>
            void operator()(std::string_view, X&) const {}
        };
    } // namespace describe_detail

    // A struct with a describe() (const or not).
    template <class T>
    concept described = requires(T& t, describe_detail::NoVisit& v) { describe(v, t); };

    namespace describe_detail {
        template <class Fn>
        struct LeafWalk {
            Fn& fn;
            std::string prefix;

            template <class X>
            void operator()(std::string_view name, X& x) {
                std::string path = prefix;
                path += name;
                if constexpr (described<X>) {
                    LeafWalk inner{fn, path + "."};
                    describe(inner, x);
                } else {
                    fn(std::string_view(path), x);
                }
            }
        };
    } // namespace describe_detail

    // Calls fn(path, field) for every leaf of `o`, nested structs flattened to
    // dotted paths ("errorbar.capsize"), in declaration order.
    template <described O, class Fn>
    void for_each_leaf(O& o, Fn&& fn) {
        describe_detail::LeafWalk<std::remove_reference_t<Fn>> walk{fn, {}};
        describe(walk, o);
    }

    // ---------------------------------------------------------------------------
    // Enum names: one spelling per value, the Python binding's canonical one.
    // ---------------------------------------------------------------------------

    template <class E>
    struct EnumName {
        std::string_view name;
        E value;
    };

    template <class E>
    struct EnumTable; // specialised per enum: `list`

#define SEXTANT_ENUM_NAMES(E, ...)                                            \
    template <>                                                               \
    struct EnumTable<E> {                                                     \
        static constexpr EnumName<E> list[] = {__VA_ARGS__};                  \
    };

    SEXTANT_ENUM_NAMES(LineStyle,
                       {"solid", LineStyle::Solid}, {"dashed", LineStyle::Dashed},
                       {"dotted", LineStyle::Dotted}, {"dashdot", LineStyle::DashDot},
                       {"none", LineStyle::None})
    SEXTANT_ENUM_NAMES(MarkerStyle,
                       {"none", MarkerStyle::None}, {"circle", MarkerStyle::Circle},
                       {"square", MarkerStyle::Square}, {"triangle", MarkerStyle::Triangle},
                       {"cross", MarkerStyle::Cross}, {"plus", MarkerStyle::Plus},
                       {"diamond", MarkerStyle::Diamond})
    SEXTANT_ENUM_NAMES(Colormap,
                       {"viridis", Colormap::Viridis}, {"plasma", Colormap::Plasma},
                       {"inferno", Colormap::Inferno}, {"magma", Colormap::Magma},
                       {"cividis", Colormap::Cividis}, {"turbo", Colormap::Turbo},
                       {"coolwarm", Colormap::Coolwarm}, {"gray", Colormap::Gray})
    SEXTANT_ENUM_NAMES(CapStyle, {"flat", CapStyle::Flat}, {"arrow", CapStyle::Arrow})
    SEXTANT_ENUM_NAMES(AxisPosition,
                       {"auto", AxisPosition::Auto}, {"low", AxisPosition::Low},
                       {"mid", AxisPosition::Mid}, {"high", AxisPosition::High})
    SEXTANT_ENUM_NAMES(LegendAnchor,
                       {"inside_tl", LegendAnchor::InsideTL}, {"inside_tr", LegendAnchor::InsideTR},
                       {"inside_bl", LegendAnchor::InsideBL}, {"inside_br", LegendAnchor::InsideBR},
                       {"outside_tl", LegendAnchor::OutsideTL},
                       {"outside_tr", LegendAnchor::OutsideTR},
                       {"outside_bl", LegendAnchor::OutsideBL},
                       {"outside_br", LegendAnchor::OutsideBR},
                       {"outside_lt", LegendAnchor::OutsideLT},
                       {"outside_lb", LegendAnchor::OutsideLB},
                       {"outside_rt", LegendAnchor::OutsideRT},
                       {"outside_rb", LegendAnchor::OutsideRB})
    SEXTANT_ENUM_NAMES(ColorbarAnchor,
                       {"left", ColorbarAnchor::Left}, {"right", ColorbarAnchor::Right},
                       {"top", ColorbarAnchor::Top}, {"bottom", ColorbarAnchor::Bottom})
    SEXTANT_ENUM_NAMES(HAlign,
                       {"left", HAlign::Left}, {"center", HAlign::Center},
                       {"right", HAlign::Right})
    SEXTANT_ENUM_NAMES(VAlign,
                       {"top", VAlign::Top}, {"center", VAlign::Center},
                       {"baseline", VAlign::Baseline}, {"bottom", VAlign::Bottom})
    SEXTANT_ENUM_NAMES(Coords, {"data", Coords::Data}, {"fraction", Coords::Fraction})
    SEXTANT_ENUM_NAMES(ArrowHead,
                       {"none", ArrowHead::None}, {"open", ArrowHead::Open},
                       {"filled", ArrowHead::Filled}, {"bar", ArrowHead::Bar})
    SEXTANT_ENUM_NAMES(PanelTheme,
                       {"dark", PanelTheme::Dark}, {"light", PanelTheme::Light},
                       {"classic", PanelTheme::Classic})
    SEXTANT_ENUM_NAMES(Projection,
                       {"orthographic", Projection::Orthographic},
                       {"perspective", Projection::Perspective})
    SEXTANT_ENUM_NAMES(PlaneOrientation,
                       {"xy", PlaneOrientation::XY}, {"yz", PlaneOrientation::YZ},
                       {"zx", PlaneOrientation::ZX})

#undef SEXTANT_ENUM_NAMES

    template <class E>
    concept named_enum = requires { EnumTable<E>::list; };

    template <named_enum E>
    constexpr std::span<const EnumName<E>> enum_names() { return EnumTable<E>::list; }

    // Empty for a value outside the table.
    template <named_enum E>
    constexpr std::string_view enum_name(E e) {
        for (const auto& n : enum_names<E>())
            if (n.value == e) return n.name;
        return {};
    }

    // Exact spelling only.
    template <named_enum E>
    constexpr std::optional<E> enum_from_name(std::string_view s) {
        for (const auto& n : enum_names<E>())
            if (n.name == s) return n.value;
        return std::nullopt;
    }

} // namespace sextant
