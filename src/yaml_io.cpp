#include "yaml_io.h"
#include "ryml_singlesrc.hpp"

#include <stdexcept>

namespace sextant::yaml {

    namespace {
        // Thrown out of ryml's parser, which is built for it (the callbacks must not return).
        [[noreturn]] void fail_basic(ryml::csubstr msg, const ryml::ErrorDataBasic&, void*) {
            throw std::invalid_argument("yaml: " + std::string(msg.str, msg.len));
        }
        [[noreturn]] void fail_parse(ryml::csubstr msg, const ryml::ErrorDataParse& d, void*) {
            throw std::invalid_argument("yaml: line " + std::to_string(d.ymlloc.line + 1) + ", column "
                                        + std::to_string(d.ymlloc.col + 1) + ": " + std::string(msg.str, msg.len));
        }
        [[noreturn]] void fail_visit(ryml::csubstr msg, const ryml::ErrorDataVisit&, void*) {
            throw std::invalid_argument("yaml: " + std::string(msg.str, msg.len));
        }

        ryml::Callbacks throwing_callbacks() {
            ryml::Callbacks cb;
            cb.set_error_basic(fail_basic).set_error_parse(fail_parse).set_error_visit(fail_visit);
            return cb;
        }

        std::string str_of(ryml::csubstr s) { return s.str ? std::string(s.str, s.len) : std::string(); }

        Node from_ryml(const ryml::ConstNodeRef& n) {
            if (n.has_key_anchor() || n.has_val_anchor() || n.is_key_ref() || n.is_val_ref())
                throw std::invalid_argument("yaml: anchors and aliases are not supported");
            if (n.is_map()) {
                Node out = Node::make_map();
                for (const ryml::ConstNodeRef& c : n.children())
                    out.add(str_of(c.key()), from_ryml(c));
                return out;
            }
            if (n.is_seq()) {
                Node out = Node::make_seq();
                for (const ryml::ConstNodeRef& c : n.children()) out.push(from_ryml(c));
                return out;
            }
            if (!n.has_val() || (n.val_is_null() && !n.is_val_quoted())) return {};
            return Node::scalar(str_of(n.val()), n.is_val_quoted());
        }

        // Whether the plain text `t` would not read back as itself (syntax, or the null
        // words). Numbers and booleans are left plain: the caller says `quoted` for a
        // string that must not read as one.
        bool needs_quotes(const std::string& t) {
            if (t.empty()) return true;
            if (t.front() == ' ' || t.back() == ' ') return true;
            static constexpr std::string_view kSpecial = "\n\t\r\"'#&*!|>%@`{}[],:?";
            if (t.find_first_of(kSpecial) != std::string::npos) return true;
            if (t.front() == '-' && (t.size() == 1 || t[1] == ' ')) return true;
            return t == "~" || t == "null" || t == "Null" || t == "NULL";
        }

        bool all_scalars(const Node& seq) {
            for (const Node& c : seq.seq) if (!c.is_scalar()) return false;
            return true;
        }

        void to_ryml(const Node& src, ryml::NodeRef dst) {
            switch (src.kind) {
            case Node::Kind::Null:
                dst.set_val("null");
                break;
            case Node::Kind::Scalar: {
                ryml::csubstr v = dst.tree()->to_arena(ryml::csubstr(src.text.data(), src.text.size()));
                dst.set_val(v);
                if (src.quoted || needs_quotes(src.text)) dst.set_val_style(ryml::VAL_DQUO);
                break;
            }
            case Node::Kind::Map:
                dst.set_map();
                for (const auto& kv : src.map) {
                    ryml::NodeRef c = dst.append_child();
                    c.set_key(c.tree()->to_arena(ryml::csubstr(kv.first.data(), kv.first.size())));
                    if (needs_quotes(kv.first)) c.set_key_style(ryml::KEY_DQUO);
                    to_ryml(kv.second, c);
                }
                break;
            case Node::Kind::Seq:
                dst.set_seq();
                if (all_scalars(src)) dst.set_container_style(ryml::FLOW_SL | ryml::FLOW_SPC);
                for (const Node& v : src.seq) to_ryml(v, dst.append_child());
                break;
            }
        }
    } // namespace

    Node parse(std::string_view text) {
        const ryml::Callbacks cb = throwing_callbacks();
        ryml::EventHandlerTree handler(cb);
        ryml::Parser parser(&handler);
        ryml::Tree tree = ryml::parse_in_arena(&parser, ryml::csubstr(text.data(), text.size()));
        if (tree.empty()) return {};
        ryml::ConstNodeRef root = tree.crootref();
        if (root.is_stream()) {
            if (root.num_children() == 0) return {};
            if (root.num_children() > 1) throw std::invalid_argument("yaml: more than one document");
            root = root.first_child();
        }
        return from_ryml(root);
    }

    std::string emit(const Node& root) {
        ryml::Tree tree(throwing_callbacks());
        tree.reserve(64);
        to_ryml(root, tree.rootref());
        return ryml::emitrs_yaml<std::string>(tree);
    }

} // namespace sextant::yaml
