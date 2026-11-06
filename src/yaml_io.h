#pragma once
// The one door to the vendored YAML library (third_party/ryml): a small value tree,
// parsed from and emitted as YAML text. Nothing else in the library includes ryml,
// so its headers, macros and symbols stay inside yaml_io.cpp. Scalars are text; the
// caller converts (a number, a bool, an enum name) and says which ones must stay
// strings. Parse errors throw std::invalid_argument with the line and column.
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sextant::yaml {

    struct Node {
        enum class Kind { Null, Scalar, Map, Seq };

        Kind        kind = Kind::Null;
        // Scalar text, escapes resolved. `quoted` = it was (or is to be written) in
        // quotes, so "1" reads back as a string and 1 as a number.
        std::string text;
        bool        quoted = false;
        // A Map in document order (keys are plain text); a Seq in order.
        std::vector<std::pair<std::string, Node>> map;
        std::vector<Node>                         seq;

        static Node scalar(std::string t, bool quoted = false) {
            Node n; n.kind = Kind::Scalar; n.text = std::move(t); n.quoted = quoted; return n;
        }
        static Node make_map() { Node n; n.kind = Kind::Map; return n; }
        static Node make_seq() { Node n; n.kind = Kind::Seq; return n; }

        bool is_scalar() const { return kind == Kind::Scalar; }
        bool is_map()    const { return kind == Kind::Map; }
        bool is_seq()    const { return kind == Kind::Seq; }

        // The value under `key` of a map, or null.
        const Node* find(std::string_view key) const {
            for (const auto& kv : map) if (kv.first == key) return &kv.second;
            return nullptr;
        }
        // Appends to a map (no duplicate check) / a seq; returns the new child.
        Node& add(std::string key, Node v) { map.emplace_back(std::move(key), std::move(v)); return map.back().second; }
        Node& push(Node v) { seq.push_back(std::move(v)); return seq.back(); }
    };

    // A document with a single root; an empty text gives a Null node. Aliases,
    // anchors and multiple documents are not supported (std::invalid_argument).
    Node parse(std::string_view text);

    // Block style; a sequence of scalars goes on one line (`[1, 2, 3]`). A string
    // that would read back as another type, or break the syntax, is quoted.
    std::string emit(const Node& root);

} // namespace sextant::yaml
