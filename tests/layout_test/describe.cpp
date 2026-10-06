// Option-struct description (GUI-kit R11): every field of every public option
// struct is listed, in declaration order, reachable for reading and writing,
// and survives a round trip through text; every enum name maps back.
#include "layout_test.h"
#include "describe.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace lt {
    using namespace sextant;

    namespace {
        template <class>
        constexpr bool kAlwaysFalse = false;

        // --- one leaf <-> text ------------------------------------------------

        std::string num(double d, int digits) {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.*g", digits, d);
            return buf;
        }

        std::string encode_color(const Color& c) {
            return num(c.r, 9) + " " + num(c.g, 9) + " " + num(c.b, 9) + " " + num(c.a, 9);
        }

        Color decode_color(const std::string& s) {
            Color c;
            std::sscanf(s.c_str(), "%f %f %f %f", &c.r, &c.g, &c.b, &c.a);
            return c;
        }

        // A vector as its size, then each element after a unit separator.
        constexpr char kSep = '\x1f';

        std::vector<std::string> split(const std::string& s) {
            std::vector<std::string> out;
            std::size_t at = s.find(kSep);
            while (at != std::string::npos) {
                const std::size_t next = s.find(kSep, at + 1);
                out.push_back(s.substr(at + 1, next == std::string::npos ? next : next - at - 1));
                at = next;
            }
            return out;
        }

        // Every leaf type describe.h promises; anything else fails to compile.
        template <class X>
        std::string encode(const X& x) {
            if constexpr (std::is_same_v<X, bool>) return x ? "true" : "false";
            else if constexpr (std::is_enum_v<X>) return std::string(enum_name(x));
            else if constexpr (std::is_same_v<X, int> || std::is_same_v<X, std::size_t>)
                return std::to_string(x);
            else if constexpr (std::is_same_v<X, float>) return num(x, 9);
            else if constexpr (std::is_same_v<X, double>) return num(x, 17);
            else if constexpr (std::is_same_v<X, std::string>) return x;
            else if constexpr (std::is_same_v<X, Color>) return encode_color(x);
            else if constexpr (std::is_same_v<X, std::optional<Color>>)
                return x ? "+" + encode_color(*x) : "-";
            else if constexpr (std::is_same_v<X, std::optional<double>>)
                return x ? "+" + num(*x, 17) : "-";
            else if constexpr (std::is_same_v<X, std::vector<double>>) {
                std::string s = std::to_string(x.size());
                for (double d : x) s += kSep + num(d, 17);
                return s;
            } else if constexpr (std::is_same_v<X, std::vector<std::string>>) {
                std::string s = std::to_string(x.size());
                for (const auto& e : x) s += kSep + e;
                return s;
            } else static_assert(kAlwaysFalse<X>, "describe.h: a leaf type the list does not name");
        }

        template <class X>
        void decode(X& x, const std::string& s) {
            if constexpr (std::is_same_v<X, bool>) x = s == "true";
            else if constexpr (std::is_enum_v<X>) x = enum_from_name<X>(s).value_or(X{});
            else if constexpr (std::is_same_v<X, int>) x = std::stoi(s);
            else if constexpr (std::is_same_v<X, std::size_t>) x = static_cast<std::size_t>(std::stoull(s));
            else if constexpr (std::is_same_v<X, float>) x = std::strtof(s.c_str(), nullptr);
            else if constexpr (std::is_same_v<X, double>) x = std::strtod(s.c_str(), nullptr);
            else if constexpr (std::is_same_v<X, std::string>) x = s;
            else if constexpr (std::is_same_v<X, Color>) x = decode_color(s);
            else if constexpr (std::is_same_v<X, std::optional<Color>>) {
                if (s == "-") x.reset();
                else x = decode_color(s.substr(1));
            } else if constexpr (std::is_same_v<X, std::optional<double>>) {
                if (s == "-") x.reset();
                else x = std::strtod(s.c_str() + 1, nullptr);
            } else if constexpr (std::is_same_v<X, std::vector<double>>) {
                x.clear();
                for (const auto& e : split(s)) x.push_back(std::strtod(e.c_str(), nullptr));
            } else if constexpr (std::is_same_v<X, std::vector<std::string>>) {
                x = split(s);
            } else static_assert(kAlwaysFalse<X>, "describe.h: a leaf type the list does not name");
        }

        // A struct as {path: text}, which is also how two of them compare.
        template <class T>
        std::map<std::string, std::string> write(const T& o) {
            std::map<std::string, std::string> doc;
            for_each_leaf(o, [&](std::string_view path, const auto& x) {
                doc[std::string(path)] = encode(x);
            });
            return doc;
        }

        template <class T>
        void read(T& o, const std::map<std::string, std::string>& doc) {
            for_each_leaf(o, [&](std::string_view path, auto& x) {
                if (auto it = doc.find(std::string(path)); it != doc.end()) decode(x, it->second);
            });
        }

        // --- a value no default has -----------------------------------------

        template <class E>
        E next_value(E e) {
            const auto names = enum_names<E>();
            for (std::size_t i = 0; i < names.size(); ++i)
                if (names[i].value == e) return names[(i + 1) % names.size()].value;
            return names[0].value;
        }

        template <class X>
        void perturb(X& x, int k) {
            const float f = 0.03125f * static_cast<float>(k + 1);
            if constexpr (std::is_same_v<X, bool>) x = !x;
            else if constexpr (std::is_enum_v<X>) x = next_value(x);
            else if constexpr (std::is_same_v<X, int>) x += 7 + k;
            else if constexpr (std::is_same_v<X, std::size_t>) x += 7 + static_cast<std::size_t>(k);
            else if constexpr (std::is_same_v<X, float>) x += 0.5f + static_cast<float>(k);
            else if constexpr (std::is_same_v<X, double>) x += 0.25 + k;
            else if constexpr (std::is_same_v<X, std::string>) x += "#" + std::to_string(k);
            else if constexpr (std::is_same_v<X, Color>) x = {x.r * 0.5f + f, 0.125f, 0.25f, 0.5f};
            else if constexpr (std::is_same_v<X, std::optional<Color>>)
                x = Color{f, 0.375f, 0.625f, 0.75f};
            else if constexpr (std::is_same_v<X, std::optional<double>>) x = 1.5 + k;
            else if constexpr (std::is_same_v<X, std::vector<double>>) {
                x.push_back(0.5 + k);
                x.push_back(-1.0 / 3.0);
            } else if constexpr (std::is_same_v<X, std::vector<std::string>>) {
                x.push_back("h" + std::to_string(k));
                x.push_back("");
                x.push_back("a b");
            } else static_assert(kAlwaysFalse<X>, "describe.h: a leaf type the list does not name");
        }

        // --- the layout check ---------------------------------------------------

        // Each direct field starts at the first offset its alignment allows
        // after the previous one, and the last ends where sizeof(T) says, so
        // no field is missing (unless it fits in padding) or out of order.
        struct LayoutWalk {
            const char* base;
            std::size_t end = 0;
            std::string bad;

            template <class X>
            void operator()(std::string_view name, const X& x) {
                const auto off = static_cast<std::size_t>(reinterpret_cast<const char*>(&x) - base);
                const std::size_t want = (end + alignof(X) - 1) / alignof(X) * alignof(X);
                if (off != want && bad.empty()) bad = std::string(name);
                end = off + sizeof(X);
            }
        };

        template <class T>
        void check_struct(const char* name) {
            const std::string who = std::string("describe ") + name + ": ";

            const T def{};
            LayoutWalk lay{reinterpret_cast<const char*>(&def)};
            describe(lay, def);
            const std::size_t padded = (lay.end + alignof(T) - 1) / alignof(T) * alignof(T);
            check(lay.bad.empty() && padded == sizeof(T),
                  who + "fields cover the struct in declaration order"
                  + (lay.bad.empty() ? "" : " (gap before '" + lay.bad + "')")
                  + " (" + std::to_string(padded) + " of " + std::to_string(sizeof(T)) + " bytes)");

            std::vector<std::string> paths;
            for_each_leaf(def, [&](std::string_view p, const auto&) { paths.emplace_back(p); });
            check(!paths.empty() && std::set<std::string>(paths.begin(), paths.end()).size() == paths.size(),
                  who + "leaf paths unique");

            T moved{};
            int k = 0;
            for_each_leaf(moved, [&](std::string_view, auto& x) { perturb(x, k++); });
            const auto d0 = write(def);
            const auto d1 = write(moved);
            std::string same;
            for (const auto& [path, text] : d1)
                if (d0.at(path) == text) same += " " + path;
            check(same.empty(), who + "every field writable" + (same.empty() ? "" : ", unchanged:" + same));

            T back{};
            read(back, d1);
            check(write(back) == d1, who + "text round trip");
        }

        template <class E>
        void check_enum(const char* name) {
            const std::string who = std::string("enum names ") + name + ": ";
            const auto names = enum_names<E>();
            std::set<std::string_view> seen;
            bool dense = true, maps_back = true;
            for (std::size_t i = 0; i < names.size(); ++i) {
                seen.insert(names[i].name);
                dense = dense && static_cast<std::size_t>(names[i].value) == i;
                maps_back = maps_back && enum_from_name<E>(enum_name(names[i].value)) == names[i].value;
            }
            check(seen.size() == names.size() && !seen.contains(""), who + "unique and non-empty");
            check(dense, who + "every value, in order");
            check(maps_back && !enum_from_name<E>("no such name"), who + "name -> value -> name");
        }
    } // namespace

    void test_describe() {
#define STRUCT(T) check_struct<T>(#T);
        STRUCT(Vec3) STRUCT(BoxAspect)
        STRUCT(ErrorBarOptions) STRUCT(LineOptions) STRUCT(ScatterOptions) STRUCT(ScatterZOptions)
        STRUCT(BarOptions) STRUCT(HistOptions) STRUCT(HeatmapOptions) STRUCT(GridOptions)
        STRUCT(AxesStyle) STRUCT(LegendOptions) STRUCT(ColorbarOptions) STRUCT(SuptitleOptions)
        STRUCT(FigureMargins)
        STRUCT(FigureOptions) STRUCT(SvgExportOptions) STRUCT(PngExportOptions)
        STRUCT(Camera3D) STRUCT(Plane2DOptions) STRUCT(Bar3DOptions) STRUCT(SurfaceOptions)
        STRUCT(SurfaceTriOptions) STRUCT(ErrorBar3DOptions) STRUCT(Scatter3DOptions)
        STRUCT(Line3DOptions) STRUCT(Box3DStyle)
#undef STRUCT

#define ENUM(E) check_enum<E>(#E);
        ENUM(LineStyle) ENUM(MarkerStyle) ENUM(Colormap) ENUM(CapStyle) ENUM(AxisPosition)
        ENUM(LegendAnchor) ENUM(ColorbarAnchor) ENUM(HAlign) ENUM(PanelTheme) ENUM(Projection)
        ENUM(PlaneOrientation)
#undef ENUM

        // Nesting flattens to dotted paths; a const struct walks too.
        const LineOptions lo{};
        const Camera3D cam{};
        const auto lw = write(lo);
        const auto cw = write(cam);
        check(lw.contains("errorbar.capsize") && cw.contains("target.z") && !cw.contains("target"),
              "describe: nested structs flatten to dotted paths");
        check(lw.at("linestyle") == "solid" && write(LegendOptions{}).at("anchor") == "outside_rt",
              "describe: enums written by name");
    }
} // namespace lt
