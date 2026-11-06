// The YAML door (src/yaml_io.h): parse, emit, round trips, quoting, errors.
// Part of sextant_layout_test.
#include "layout_test.h"
#include "yaml_io.h"
#include <cstdio>
#include <stdexcept>

namespace lt {
    namespace {
        using sextant::yaml::Node;

        bool throws(std::string_view text) {
            try { (void)sextant::yaml::parse(text); } catch (const std::invalid_argument&) { return true; }
            return false;
        }

        // Same shape and the same text/quoted flags, all the way down.
        bool same(const Node& a, const Node& b) {
            if (a.kind != b.kind) return false;
            switch (a.kind) {
            case Node::Kind::Null:   return true;
            case Node::Kind::Scalar: return a.text == b.text;
            case Node::Kind::Seq:
                if (a.seq.size() != b.seq.size()) return false;
                for (std::size_t i = 0; i < a.seq.size(); ++i)
                    if (!same(a.seq[i], b.seq[i])) return false;
                return true;
            case Node::Kind::Map:
                if (a.map.size() != b.map.size()) return false;
                for (std::size_t i = 0; i < a.map.size(); ++i)
                    if (a.map[i].first != b.map[i].first || !same(a.map[i].second, b.map[i].second)) return false;
                return true;
            }
            return false;
        }
    } // namespace

    void test_yaml_io() {
        namespace y = sextant::yaml;

        // Parse: nesting, order, flow and block sequences, plain vs quoted.
        const Node n = y::parse("a: 1\nb:\n  c: [1, 2.5, x]\n  d: \"12\"\nlist:\n  - p\n  - q: 3\n");
        check(n.is_map() && n.map.size() == 3 && n.map[0].first == "a" && n.map[2].first == "list",
              "yaml: a map keeps document order");
        check(n.find("a") && n.find("a")->text == "1" && !n.find("a")->quoted, "yaml: a plain scalar");
        const Node* b = n.find("b");
        check(b && b->find("c") && b->find("c")->is_seq() && b->find("c")->seq.size() == 3
                  && b->find("c")->seq[1].text == "2.5",
              "yaml: a flow sequence");
        check(b && b->find("d") && b->find("d")->text == "12" && b->find("d")->quoted,
              "yaml: a quoted scalar stays a string");
        const Node* list = n.find("list");
        check(list && list->seq.size() == 2 && list->seq[1].is_map() && list->seq[1].find("q")->text == "3",
              "yaml: a block sequence of scalar and map");

        // Empty and comment-only documents are Null.
        check(y::parse("").kind == Node::Kind::Null && y::parse("# nothing\n").kind == Node::Kind::Null,
              "yaml: an empty document is Null");

        // Emit then parse gives the same tree, strings that look like other types included.
        Node r = Node::make_map();
        r.add("format", Node::scalar("1"));
        r.add("name", Node::scalar("1", true));                                   // a string "1", not a number
        Node tricky = Node::make_seq();
        for (const char* s : {"", " lead", "trail ", "a: b", "# c", "yes", "null", "-", "- x", "1e3", "0x1F",
                              "line\nbreak", "tab\there", "quote\"s", "it's", "{x}", "[y]", "a,b", "~", "on", "päth/é"})
            tricky.push(Node::scalar(s, true));
        r.add("tricky", std::move(tricky));
        Node nums = Node::make_seq();
        for (const char* s : {"1", "-2.5", "3e-4", "1/3"}) nums.push(Node::scalar(s));
        r.add("nums", std::move(nums));
        Node deep = Node::make_map();
        deep.add("k with space", Node::scalar("v"));
        deep.add("empty_seq", Node::make_seq());
        deep.add("empty_map", Node::make_map());
        Node inner = Node::make_seq();
        Node m1 = Node::make_map(); m1.add("x", Node::scalar("1")); m1.add("y", Node::scalar("two", true));
        inner.push(std::move(m1));
        inner.push(Node::scalar("plain"));
        deep.add("items", std::move(inner));
        r.add("deep", std::move(deep));

        const std::string text = y::emit(r);
        Node back;
        bool ok = true;
        try { back = y::parse(text); } catch (const std::exception& e) { ok = false; check(false, std::string("yaml: emitted text does not parse: ") + e.what()); }
        if (ok) {
            check(same(r, back), "yaml: emit then parse round-trips the tree");
            if (!same(r, back)) std::fprintf(stderr, "--- emitted ---\n%s\n--- end ---\n", text.c_str());
        }
        check(text.find("nums: [1, -2.5, 3e-4, 1/3]") != std::string::npos,
              "yaml: a sequence of scalars is on one line");
        const Node* nm = back.find("name");
        check(nm && nm->text == "1" && nm->quoted, "yaml: the string \"1\" comes back quoted");
        const Node* fm = back.find("format");
        check(fm && fm->text == "1" && !fm->quoted, "yaml: the number 1 comes back unquoted");

        // Errors are std::invalid_argument, with a position; no abort.
        check(throws("a: [1, 2"), "yaml: an unclosed flow sequence throws");
        check(throws("a: 1\n  b: 2\n c: 3"), "yaml: bad indentation throws");
        check(throws("a: &x 1\nb: *x"), "yaml: an alias is refused");
        check(throws("---\na: 1\n---\nb: 2\n"), "yaml: two documents are refused");
        try {
            (void)y::parse("a: 1\nb: [\n");
            check(false, "yaml: error text names the line");
        } catch (const std::invalid_argument& e) {
            check(std::string(e.what()).find("line") != std::string::npos, "yaml: error text names the line");
        }
        // The parser stays usable after an error (no global state left behind).
        check(y::parse("z: 9\n").find("z")->text == "9", "yaml: parsing works after an error");
    }
} // namespace lt
