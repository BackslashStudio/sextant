#pragma once
// Any `described<T>` struct (describe.h) <-> a yaml::Node tree, the layer
// the style files are made of. Internal: no public header includes this.
//
//     Node n = to_node(opts);            // every field
//     Node d = to_node(opts, LineOptions{});  // only the fields that differ
//     from_node(n, opts, issues);        // absent keys leave a field unchanged
//
// The format, per leaf type:
//   bool            true / false
//   int, size_t     123
//   float, double   shortest text that reads back exactly; .nan .inf -.inf
//   std::string     text (quoted when it would read as a number, bool or null)
//   Color           "#rrggbb" / "#rrggbbaa" when every channel is a multiple of
//                   1/255 (the common case), else [r, g, b, a]; read back also
//                   as a name ("red") or a 3-element [r, g, b]
//   Colormap        its name ("viridis", "plasma_r")
//   enum            enum_name() ("dashed", "inside_tr")
//   optional<T>     T, or null for an empty one
//   Range           [lo, hi]
//   vector<T>       [a, b, c]
//   nested struct   a mapping of its own fields
//
// Data never goes into a style: `hint_labels` and the per-point `sizes` /
// `bottom` arrays are skipped on write (see is_data_field). They are read when
// a document has them, so a hand-written file may still set them.
//
// Reading never throws for content: a bad value, an unknown key or a wrong
// shape adds a line to `Issues` and leaves that field as it was.
#include "describe.h"
#include "yaml_io.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#if __has_include(<version>)
#include <version>
#endif

namespace sextant::yaml {

    struct Issues {
        std::vector<std::string> list;
        void add(std::string line) { list.push_back(std::move(line)); }
        bool empty() const { return list.empty(); }
    };

    // Fields that are data, not style (the per-point arrays an options struct carries).
    template <class X>
    inline bool is_data_field(std::string_view name, const X&) {
        if (name == "hint_labels") return true;
        if constexpr (std::is_same_v<X, std::vector<double>>) return name == "sizes" || name == "bottom";
        return false;
    }

    namespace detail {
        template <class>
        constexpr bool kAlwaysFalse = false;
        template <class X>
        struct IsOptional : std::false_type {};
        template <class X>
        struct IsOptional<std::optional<X>> : std::true_type {};
        template <class X>
        struct IsVector : std::false_type {};
        template <class X, class A>
        struct IsVector<std::vector<X, A>> : std::true_type {};

        inline std::string join(const std::string& path, std::string_view name) {
            return path.empty() ? std::string(name) : path + "." + std::string(name);
        }

        // --- numbers --------------------------------------------------------

        template <class Fp>
        std::string format_fp(Fp x) {
            if (std::isnan(x)) return ".nan";
            if (std::isinf(x)) return x > 0 ? ".inf" : "-.inf";
            char buf[48];
#if defined(__cpp_lib_to_chars)
            const auto r = std::to_chars(buf, buf + sizeof buf, x);
            return std::string(buf, r.ptr);
#else
            // No shortest-text to_chars here: the fewest digits that read back.
            for (int p = 1; p <= std::numeric_limits<Fp>::max_digits10; ++p) {
                std::snprintf(buf, sizeof buf, "%.*g", p, static_cast<double>(x));
                if (static_cast<Fp>(std::strtod(buf, nullptr)) == x) break;
            }
            return buf;
#endif
        }

        inline std::string lower(std::string s) {
            for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        // nan / inf spellings; returns false if `s` is not one.
        template <class Fp>
        bool special_fp(const std::string& s, Fp& out) {
            const std::string l = lower(s);
            if (l == ".nan" || l == "nan") { out = std::numeric_limits<Fp>::quiet_NaN(); return true; }
            if (l == ".inf" || l == "+.inf" || l == "inf" || l == "+inf") { out = std::numeric_limits<Fp>::infinity(); return true; }
            if (l == "-.inf" || l == "-inf") { out = -std::numeric_limits<Fp>::infinity(); return true; }
            return false;
        }

        template <class Fp>
        bool parse_fp(const std::string& s, Fp& out) {
            if (s.empty()) return false;
            if (special_fp(s, out)) return true;
            const char* b = s.data();
            const char* e = b + s.size();
            if (*b == '+') ++b;
            if (b == e) return false;
#if defined(__cpp_lib_to_chars)
            Fp v{};
            const auto r = std::from_chars(b, e, v);
            if (r.ec != std::errc{} || r.ptr != e) return false;
            out = v;
            return true;
#else
            if (*b == ' ' || *b == '\t') return false;
            char* end = nullptr;
            const double v = std::strtod(b, &end);
            if (end != e) return false;
            out = static_cast<Fp>(v);
            return true;
#endif
        }

        template <class I>
        bool parse_int(const std::string& s, I& out) {
            if (s.empty()) return false;
            I v{};
            const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return false;
            out = v;
            return true;
        }

        // A string that a YAML reader would take for another type.
        inline bool looks_typed(const std::string& s) {
            if (s.empty()) return false;
            double d;
            if (parse_fp(s, d)) return true;
            const std::string l = lower(s);
            for (const char* w : {"true", "false", "null", "~", "yes", "no", "on", "off", "y", "n"})
                if (l == w) return true;
            return false;
        }

        inline std::string got(const Node& n) {
            switch (n.kind) {
                case Node::Kind::Null:   return "nothing";
                case Node::Kind::Scalar: return (n.quoted ? "the string \"" : "'") + n.text + (n.quoted ? "\"" : "'");
                case Node::Kind::Map:    return "a mapping";
                case Node::Kind::Seq:    return "a list";
            }
            return "?";
        }

        inline bool is_null(const Node& n) {
            if (n.kind == Node::Kind::Null) return true;
            return n.is_scalar() && !n.quoted && (n.text == "null" || n.text == "~" || n.text == "Null" || n.text == "NULL");
        }

        // --- a leaf -> a node -------------------------------------------------

        inline bool hex_exact(const Color& c, unsigned (&k)[4]) {
            const float ch[4] = {c.r, c.g, c.b, c.a};
            for (int i = 0; i < 4; ++i) {
                if (!(ch[i] >= 0.0f && ch[i] <= 1.0f)) return false;
                k[i] = static_cast<unsigned>(std::lround(ch[i] * 255.0f));
                if (static_cast<float>(k[i]) / 255.0f != ch[i]) return false;
            }
            return true;
        }

        template <class X>
        Node encode_leaf(const X& x) {
            if constexpr (std::is_same_v<X, bool>) return Node::scalar(x ? "true" : "false");
            else if constexpr (std::is_enum_v<X>) return Node::scalar(std::string(enum_name(x)));
            else if constexpr (std::is_same_v<X, Colormap>) return Node::scalar(x.name());
            else if constexpr (std::is_same_v<X, int> || std::is_same_v<X, std::size_t>) return Node::scalar(std::to_string(x));
            else if constexpr (std::is_same_v<X, float> || std::is_same_v<X, double>) return Node::scalar(format_fp(x));
            else if constexpr (std::is_same_v<X, std::string>) return Node::scalar(x, looks_typed(x));
            else if constexpr (std::is_same_v<X, Color>) {
                unsigned k[4];
                if (hex_exact(x, k)) {
                    char buf[16];
                    if (k[3] == 255) std::snprintf(buf, sizeof buf, "#%02x%02x%02x", k[0], k[1], k[2]);
                    else std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", k[0], k[1], k[2], k[3]);
                    return Node::scalar(buf, true);
                }
                Node s = Node::make_seq();
                for (float v : {x.r, x.g, x.b, x.a}) s.push(Node::scalar(format_fp(v)));
                return s;
            } else if constexpr (std::is_same_v<X, Range>) {
                Node s = Node::make_seq();
                s.push(Node::scalar(format_fp(x.lo)));
                s.push(Node::scalar(format_fp(x.hi)));
                return s;
            } else if constexpr (IsOptional<X>::value) {
                if (!x) return Node::scalar("null");
                return encode_leaf(*x);
            } else if constexpr (IsVector<X>::value) {
                Node s = Node::make_seq();
                for (const auto& e : x) s.push(encode_leaf(static_cast<typename X::value_type>(e)));
                return s;
            } else static_assert(kAlwaysFalse<X>, "yaml_describe.h: a leaf type the list does not name");
        }

        // --- a node -> a leaf; x is untouched when this returns false -------------

        template <class X>
        bool decode_leaf(const Node& n, X& x, std::string& err) {
            const auto fail = [&](const char* want) {
                err = std::string("expected ") + want + ", got " + got(n);
                return false;
            };
            if constexpr (std::is_same_v<X, bool>) {
                if (n.is_scalar() && !n.quoted && (n.text == "true" || n.text == "false")) { x = n.text == "true"; return true; }
                return fail("true or false");
            } else if constexpr (std::is_enum_v<X>) {
                if (n.is_scalar()) {
                    if (const auto e = enum_from_name<X>(n.text)) { x = *e; return true; }
                    err = "unknown value '" + n.text + "', one of:";
                    for (const auto& en : enum_names<X>()) err += " " + std::string(en.name);
                    return false;
                }
                return fail("a name");
            } else if constexpr (std::is_same_v<X, Colormap>) {
                if (n.is_scalar()) {
                    try { x = Colormap::from_name(n.text); return true; }
                    catch (const std::invalid_argument&) { err = "unknown colormap '" + n.text + "'"; return false; }
                }
                return fail("a colormap name");
            } else if constexpr (std::is_same_v<X, int> || std::is_same_v<X, std::size_t>) {
                X v{};
                if (n.is_scalar() && !n.quoted && parse_int(n.text, v)) { x = v; return true; }
                return fail("an integer");
            } else if constexpr (std::is_same_v<X, float> || std::is_same_v<X, double>) {
                X v{};
                if (n.is_scalar() && !n.quoted && parse_fp(n.text, v)) { x = v; return true; }
                return fail("a number");
            } else if constexpr (std::is_same_v<X, std::string>) {
                if (n.is_scalar()) { x = n.text; return true; }
                if (n.kind == Node::Kind::Null) { x.clear(); return true; }
                return fail("text");
            } else if constexpr (std::is_same_v<X, Color>) {
                if (n.is_scalar()) {
                    try { x = Color::from_name(n.text); return true; }
                    catch (const std::invalid_argument& e) { err = e.what(); return false; }
                }
                if (n.is_seq() && (n.seq.size() == 3 || n.seq.size() == 4)) {
                    Color c = x;
                    c.a = 1.0f;
                    float* ch[4] = {&c.r, &c.g, &c.b, &c.a};
                    for (std::size_t i = 0; i < n.seq.size(); ++i) {
                        std::string sub;
                        if (!decode_leaf(n.seq[i], *ch[i], sub)) { err = "[" + std::to_string(i) + "]: " + sub; return false; }
                    }
                    x = c;
                    return true;
                }
                return fail("a color (\"#rrggbb\", a name, or [r, g, b, a])");
            } else if constexpr (std::is_same_v<X, Range>) {
                if (n.is_seq() && n.seq.size() == 2) {
                    Range r = x;
                    std::string sub;
                    if (!decode_leaf(n.seq[0], r.lo, sub) || !decode_leaf(n.seq[1], r.hi, sub)) { err = sub; return false; }
                    x = r;
                    return true;
                }
                return fail("[lo, hi]");
            } else if constexpr (IsOptional<X>::value) {
                if (is_null(n)) { x.reset(); return true; }
                typename X::value_type v{};
                if (x) v = *x;
                if (!decode_leaf(n, v, err)) return false;
                x = v;
                return true;
            } else if constexpr (IsVector<X>::value) {
                if (!n.is_seq()) return fail("a list");
                X out;
                for (std::size_t i = 0; i < n.seq.size(); ++i) {
                    typename X::value_type e{};
                    std::string sub;
                    if (!decode_leaf(n.seq[i], e, sub)) { err = "[" + std::to_string(i) + "]: " + sub; return false; }
                    out.push_back(std::move(e));
                }
                x = std::move(out);
                return true;
            } else static_assert(kAlwaysFalse<X>, "yaml_describe.h: a leaf type the list does not name");
        }

        // --- the two visitors ---------------------------------------------------

        struct Writer {
            Node& out;

            template <class X>
            void operator()(std::string_view name, const X& x) {
                if (is_data_field(name, x)) return;
                if constexpr (described<X>) {
                    Node child = Node::make_map();
                    Writer inner{child};
                    describe(inner, x);
                    out.add(std::string(name), std::move(child));
                } else {
                    out.add(std::string(name), encode_leaf(x));
                }
            }
        };

        struct Reader {
            const Node& in;
            Issues& issues;
            std::string path;
            std::vector<std::string_view> seen;

            template <class X>
            void operator()(std::string_view name, X& x) {
                seen.push_back(name);
                const Node* n = in.find(name);
                if (!n) return;
                const std::string at = join(path, name);
                if constexpr (described<X>) {
                    read_struct(*n, x, issues, at);
                } else {
                    std::string err;
                    if (!decode_leaf(*n, x, err)) issues.add(at + ": " + err);
                }
            }

            // One line naming every key of this mapping that no field answers to.
            void report_unknown() {
                std::string keys;
                for (const auto& kv : in.map) {
                    bool known = false;
                    for (std::string_view s : seen) known = known || s == kv.first;
                    if (!known) keys += (keys.empty() ? "" : ", ") + kv.first;
                }
                if (!keys.empty()) issues.add((path.empty() ? std::string("(top level)") : path) + ": unknown keys skipped: " + keys);
            }

            template <class X>
            static void read_struct(const Node& n, X& x, Issues& issues, const std::string& at) {
                if (n.kind == Node::Kind::Null) return; // "errorbar:" with nothing under it
                if (!n.is_map()) {
                    issues.add((at.empty() ? std::string("(top level)") : at) + ": expected a mapping, got " + got(n));
                    return;
                }
                Reader r{n, issues, at, {}};
                describe(r, x);
                r.report_unknown();
            }
        };

        inline bool node_equal(const Node& a, const Node& b) {
            if (a.kind != b.kind) return false;
            switch (a.kind) {
                case Node::Kind::Null:   return true;
                case Node::Kind::Scalar: return a.text == b.text;
                case Node::Kind::Seq:
                    if (a.seq.size() != b.seq.size()) return false;
                    for (std::size_t i = 0; i < a.seq.size(); ++i)
                        if (!node_equal(a.seq[i], b.seq[i])) return false;
                    return true;
                case Node::Kind::Map:
                    if (a.map.size() != b.map.size()) return false;
                    for (std::size_t i = 0; i < a.map.size(); ++i)
                        if (a.map[i].first != b.map[i].first || !node_equal(a.map[i].second, b.map[i].second)) return false;
                    return true;
            }
            return false;
        }

        // Drops from `a` what `base` already says; a mapping left empty goes too.
        inline void prune(Node& a, const Node& base) {
            std::vector<std::pair<std::string, Node>> keep;
            for (auto& kv : a.map) {
                const Node* b = base.find(kv.first);
                if (b && kv.second.is_map() && b->is_map()) {
                    prune(kv.second, *b);
                    if (kv.second.map.empty()) continue;
                } else if (b && node_equal(kv.second, *b)) {
                    continue;
                }
                keep.push_back(std::move(kv));
            }
            a.map = std::move(keep);
        }
    } // namespace detail

    // Every field of `o` (data fields excepted).
    template <described T>
    Node to_node(const T& o) {
        Node n = Node::make_map();
        detail::Writer w{n};
        describe(w, o);
        return n;
    }

    // Only the fields of `o` that differ from `base`'s; a struct equal to
    // `base` gives an empty mapping.
    template <described T>
    Node to_node(const T& o, const T& base) {
        Node n = to_node(o);
        detail::prune(n, to_node(base));
        return n;
    }

    // Sets the fields `n` names; absent ones keep their value. `path` heads
    // the issue lines.
    template <described T>
    void from_node(const Node& n, T& o, Issues& issues, const std::string& path = {}) {
        detail::Reader::read_struct(n, o, issues, path);
    }

} // namespace sextant::yaml
