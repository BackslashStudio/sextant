#include "rich_text.h"
#include "messages.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace sextant {
namespace {

// ---- Atoms ------------------------------------------------------------------

// TeX's atom classes, which decide the space between neighbours.
enum class Cls { Ord, Op, Bin, Rel, Open, Close, Punct };

struct Atom {
    enum class Kind { Glyphs, Group, Space } kind = Kind::Glyphs;
    Cls cls = Cls::Ord;
    std::string text;              // Glyphs
    float em = 0.0f;               // Space, in ems of the current size
    std::vector<Atom> group;       // Group
    std::vector<Atom> sub, sup;
    bool has_sub = false, has_sup = false;
};
using MathList = std::vector<Atom>;

// A line: text outside the spans, and each span's list.
struct Segment {
    bool math = false;
    std::string text;   // !math, `\$` already a dollar
    MathList list;      // math
};

struct Parsed {
    std::vector<Segment> segments;
    std::optional<MathError> error;
};

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string utf8(char32_t cp) {
    std::string s;
    append_utf8(s, cp);
    return s;
}

// Bytes in the UTF-8 sequence that starts with `b0` (1 for a stray byte).
std::size_t utf8_len(unsigned char b0) {
    if ((b0 & 0xE0) == 0xC0) return 2;
    if ((b0 & 0xF0) == 0xE0) return 3;
    if ((b0 & 0xF8) == 0xF0) return 4;
    return 1;
}

// ---- The symbol table -------------------------------------------------------

struct Symbol {
    std::string_view name;
    char32_t cp;
    Cls cls;
};

// `\name` -> one character. TeX's names and classes; matplotlib's choices
// where the two differ (\epsilon is the lunate one, \phi the closed one).
constexpr Symbol kSymbols[] = {
    // Greek, lower case
    {"alpha", 0x03B1, Cls::Ord}, {"beta", 0x03B2, Cls::Ord}, {"gamma", 0x03B3, Cls::Ord},
    {"delta", 0x03B4, Cls::Ord}, {"epsilon", 0x03F5, Cls::Ord}, {"varepsilon", 0x03B5, Cls::Ord},
    {"zeta", 0x03B6, Cls::Ord}, {"eta", 0x03B7, Cls::Ord}, {"theta", 0x03B8, Cls::Ord},
    {"vartheta", 0x03D1, Cls::Ord}, {"iota", 0x03B9, Cls::Ord}, {"kappa", 0x03BA, Cls::Ord},
    {"lambda", 0x03BB, Cls::Ord}, {"mu", 0x03BC, Cls::Ord}, {"nu", 0x03BD, Cls::Ord},
    {"xi", 0x03BE, Cls::Ord}, {"omicron", 0x03BF, Cls::Ord}, {"pi", 0x03C0, Cls::Ord},
    {"varpi", 0x03D6, Cls::Ord}, {"rho", 0x03C1, Cls::Ord}, {"varrho", 0x03F1, Cls::Ord},
    {"sigma", 0x03C3, Cls::Ord}, {"varsigma", 0x03C2, Cls::Ord}, {"tau", 0x03C4, Cls::Ord},
    {"upsilon", 0x03C5, Cls::Ord}, {"phi", 0x03D5, Cls::Ord}, {"varphi", 0x03C6, Cls::Ord},
    {"chi", 0x03C7, Cls::Ord}, {"psi", 0x03C8, Cls::Ord}, {"omega", 0x03C9, Cls::Ord},
    // Greek, upper case (the ones that differ from Latin)
    {"Gamma", 0x0393, Cls::Ord}, {"Delta", 0x0394, Cls::Ord}, {"Theta", 0x0398, Cls::Ord},
    {"Lambda", 0x039B, Cls::Ord}, {"Xi", 0x039E, Cls::Ord}, {"Pi", 0x03A0, Cls::Ord},
    {"Sigma", 0x03A3, Cls::Ord}, {"Upsilon", 0x03A5, Cls::Ord}, {"Phi", 0x03A6, Cls::Ord},
    {"Psi", 0x03A8, Cls::Ord}, {"Omega", 0x03A9, Cls::Ord},
    // Binary operators
    {"pm", 0x00B1, Cls::Bin}, {"mp", 0x2213, Cls::Bin}, {"times", 0x00D7, Cls::Bin},
    {"div", 0x00F7, Cls::Bin}, {"cdot", 0x22C5, Cls::Bin}, {"ast", 0x2217, Cls::Bin},
    {"star", 0x22C6, Cls::Bin}, {"circ", 0x2218, Cls::Bin}, {"bullet", 0x2219, Cls::Bin},
    {"oplus", 0x2295, Cls::Bin}, {"ominus", 0x2296, Cls::Bin}, {"otimes", 0x2297, Cls::Bin},
    {"odot", 0x2299, Cls::Bin}, {"cap", 0x2229, Cls::Bin}, {"cup", 0x222A, Cls::Bin},
    {"wedge", 0x2227, Cls::Bin}, {"land", 0x2227, Cls::Bin}, {"vee", 0x2228, Cls::Bin},
    {"lor", 0x2228, Cls::Bin}, {"setminus", 0x2216, Cls::Bin}, {"dagger", 0x2020, Cls::Bin},
    {"ddagger", 0x2021, Cls::Bin},
    // Relations
    {"leq", 0x2264, Cls::Rel}, {"le", 0x2264, Cls::Rel}, {"geq", 0x2265, Cls::Rel},
    {"ge", 0x2265, Cls::Rel}, {"neq", 0x2260, Cls::Rel}, {"ne", 0x2260, Cls::Rel},
    {"approx", 0x2248, Cls::Rel}, {"sim", 0x223C, Cls::Rel}, {"simeq", 0x2243, Cls::Rel},
    {"cong", 0x2245, Cls::Rel}, {"equiv", 0x2261, Cls::Rel}, {"propto", 0x221D, Cls::Rel},
    {"ll", 0x226A, Cls::Rel}, {"gg", 0x226B, Cls::Rel}, {"in", 0x2208, Cls::Rel},
    {"notin", 0x2209, Cls::Rel}, {"ni", 0x220B, Cls::Rel}, {"subset", 0x2282, Cls::Rel},
    {"supset", 0x2283, Cls::Rel}, {"subseteq", 0x2286, Cls::Rel}, {"supseteq", 0x2287, Cls::Rel},
    {"perp", 0x22A5, Cls::Rel}, {"parallel", 0x2225, Cls::Rel}, {"mid", 0x2223, Cls::Rel},
    {"to", 0x2192, Cls::Rel}, {"rightarrow", 0x2192, Cls::Rel}, {"leftarrow", 0x2190, Cls::Rel},
    {"gets", 0x2190, Cls::Rel}, {"leftrightarrow", 0x2194, Cls::Rel},
    {"Rightarrow", 0x21D2, Cls::Rel}, {"Leftarrow", 0x21D0, Cls::Rel},
    {"Leftrightarrow", 0x21D4, Cls::Rel}, {"iff", 0x21D4, Cls::Rel}, {"implies", 0x21D2, Cls::Rel},
    {"mapsto", 0x21A6, Cls::Rel}, {"uparrow", 0x2191, Cls::Rel}, {"downarrow", 0x2193, Cls::Rel},
    {"longrightarrow", 0x27F6, Cls::Rel}, {"longleftarrow", 0x27F5, Cls::Rel},
    // Large operators (side scripts only; limits above/below are deferred)
    {"sum", 0x2211, Cls::Op}, {"prod", 0x220F, Cls::Op}, {"coprod", 0x2210, Cls::Op},
    {"int", 0x222B, Cls::Op}, {"iint", 0x222C, Cls::Op}, {"oint", 0x222E, Cls::Op},
    {"bigcup", 0x22C3, Cls::Op}, {"bigcap", 0x22C2, Cls::Op},
    // Ordinary symbols
    {"infty", 0x221E, Cls::Ord}, {"partial", 0x2202, Cls::Ord}, {"nabla", 0x2207, Cls::Ord},
    {"forall", 0x2200, Cls::Ord}, {"exists", 0x2203, Cls::Ord}, {"emptyset", 0x2205, Cls::Ord},
    {"varnothing", 0x2205, Cls::Ord}, {"neg", 0x00AC, Cls::Ord}, {"lnot", 0x00AC, Cls::Ord},
    {"angle", 0x2220, Cls::Ord}, {"triangle", 0x25B3, Cls::Ord}, {"degree", 0x00B0, Cls::Ord},
    {"prime", 0x2032, Cls::Ord}, {"ell", 0x2113, Cls::Ord}, {"hbar", 0x210F, Cls::Ord},
    {"Re", 0x211C, Cls::Ord}, {"Im", 0x2111, Cls::Ord}, {"aleph", 0x2135, Cls::Ord},
    {"wp", 0x2118, Cls::Ord}, {"ldots", 0x2026, Cls::Ord}, {"dots", 0x2026, Cls::Ord},
    {"cdots", 0x22EF, Cls::Ord}, {"vdots", 0x22EE, Cls::Ord}, {"ddots", 0x22F1, Cls::Ord},
    {"dag", 0x2020, Cls::Ord}, {"ddag", 0x2021, Cls::Ord}, {"S", 0x00A7, Cls::Ord},
    {"P", 0x00B6, Cls::Ord}, {"copyright", 0x00A9, Cls::Ord}, {"AA", 0x00C5, Cls::Ord},
    {"sqrt", 0x221A, Cls::Ord}, {"surd", 0x221A, Cls::Ord}, {"backslash", 0x005C, Cls::Ord},
    {"top", 0x22A4, Cls::Ord}, {"bot", 0x22A5, Cls::Ord},
    // Delimiters (their own size; \left/\right are deferred)
    {"langle", 0x27E8, Cls::Open}, {"rangle", 0x27E9, Cls::Close},
    {"lfloor", 0x230A, Cls::Open}, {"rfloor", 0x230B, Cls::Close},
    {"lceil", 0x2308, Cls::Open}, {"rceil", 0x2309, Cls::Close},
    {"lbrace", 0x007B, Cls::Open}, {"rbrace", 0x007D, Cls::Close},
};

// Upright function names, TeX's \sin and friends: class Op.
constexpr std::string_view kFunctions[] = {
    "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh",
    "tanh", "coth", "log", "ln", "lg", "exp", "lim", "liminf", "limsup", "max", "min", "sup",
    "inf", "det", "dim", "deg", "arg", "ker", "gcd", "hom", "Pr",
};

// Spacing commands, in ems.
struct SpaceCmd {
    std::string_view name;
    float em;
};
constexpr SpaceCmd kSpaces[] = {
    {"quad", 1.0f}, {"qquad", 2.0f}, {"enspace", 0.5f},
    {"thinspace", 1.0f / 6.0f}, {"medspace", 2.0f / 9.0f}, {"thickspace", 5.0f / 18.0f},
};

// Commands known from mathtext that this step does not draw yet.
constexpr std::string_view kDeferred[] = {
    "frac", "dfrac", "tfrac", "binom", "left", "right", "big", "Big", "bigg", "Bigg",
    "hat", "widehat", "bar", "vec", "dot", "ddot", "tilde", "widetilde", "acute", "grave",
    "breve", "check", "overline", "underline", "mathbf", "boldsymbol", "mathcal", "mathbb",
    "mathfrak", "mathscr", "mathsf", "mathtt", "color", "stackrel", "overset", "underset",
    "operatorname", "substack",
};

template <class T, std::size_t N>
bool contains(const T (&list)[N], std::string_view name) {
    return std::find(std::begin(list), std::end(list), name) != std::end(list);
}

// ---- The parser -------------------------------------------------------------

// Thrown inside the parser only; never leaves this file.
struct ParseFail {
    std::size_t pos;
    std::string reason;
};

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    // One line's segments; the caller checked has_math().
    std::vector<Segment> line() {
        std::vector<Segment> out;
        Segment text;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '\\' && i_ + 1 < s_.size() && s_[i_ + 1] == '$') {
                text.text += '$';
                i_ += 2;
            } else if (c == '$') {
                if (!text.text.empty()) out.push_back(std::move(text));
                text = Segment{};
                ++i_;
                Segment m;
                m.math = true;
                m.list = list(End::Dollar);
                out.push_back(std::move(m));
            } else {
                text.text += c;
                ++i_;
            }
        }
        if (!text.text.empty()) out.push_back(std::move(text));
        return out;
    }

private:
    enum class End { Dollar, Brace };

    std::string_view s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(std::size_t pos, std::string reason) const {
        throw ParseFail{pos, std::move(reason)};
    }

    void skip_spaces() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t')) ++i_;
    }

    // Atoms up to the closing `$` or `}` (consumed).
    MathList list(End end) {
        MathList out;
        const std::size_t open = i_ - 1;
        for (;;) {
            if (i_ >= s_.size()) {
                if (end == End::Brace) fail(open, "'{' is never closed");
                fail(open, "'$' is never closed");
            }
            const char c = s_[i_];
            if (c == ' ' || c == '\t') {
                ++i_;   // TeX: a typed space does nothing in math
            } else if (c == '$') {
                if (end == End::Brace) fail(open, "'{' is never closed");
                ++i_;
                break;
            } else if (c == '}') {
                if (end != End::Brace) fail(i_, "'}' without a '{'");
                ++i_;
                break;
            } else if (c == '^' || c == '_') {
                const bool sup = c == '^';
                const std::size_t at = i_++;
                if (out.empty() || out.back().kind == Atom::Kind::Space) {
                    Atom empty;
                    empty.kind = Atom::Kind::Group;
                    out.push_back(std::move(empty));
                }
                Atom& a = out.back();
                if (sup ? a.has_sup : a.has_sub)
                    fail(at, sup ? "double superscript" : "double subscript");
                MathList arg = script_arg(at);
                (sup ? a.sup : a.sub) = std::move(arg);
                (sup ? a.has_sup : a.has_sub) = true;
            } else {
                atom(out);
            }
        }
        fix_binaries(out);
        return out;
    }

    // One atom or group as a script's argument.
    MathList script_arg(std::size_t at) {
        skip_spaces();
        if (i_ >= s_.size() || s_[i_] == '$' || s_[i_] == '}')
            fail(at, std::string("'") + s_[at] + "' has nothing to apply to");
        if (s_[i_] == '^' || s_[i_] == '_')
            fail(i_, std::string("'") + s_[i_] + "' cannot start a script");
        MathList arg;
        atom(arg);
        if (arg.size() == 1 && arg.front().kind == Atom::Kind::Group && !arg.front().has_sub
            && !arg.front().has_sup)
            return std::move(arg.front().group);
        return arg;
    }

    // One character, group or command, appended to `out`.
    void atom(MathList& out) {
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            Atom g;
            g.kind = Atom::Kind::Group;
            g.group = list(End::Brace);
            out.push_back(std::move(g));
            return;
        }
        if (c == '\\') {
            command(out);
            return;
        }
        if (c == '~') {
            ++i_;
            out.push_back(space(1.0f / 3.0f));
            return;
        }
        const std::size_t n = std::min(utf8_len(static_cast<unsigned char>(c)), s_.size() - i_);
        Atom a;
        a.text = std::string(s_.substr(i_, n));
        i_ += n;
        if (n == 1) {
            switch (c) {
                case '+': case '*': a.cls = Cls::Bin; break;
                case '-': a.cls = Cls::Bin; a.text = utf8(0x2212); break;   // the minus sign
                case '=': case '<': case '>': case ':': a.cls = Cls::Rel; break;
                case ',': case ';': a.cls = Cls::Punct; break;
                case '(': case '[': a.cls = Cls::Open; break;
                case ')': case ']': case '!': case '?': a.cls = Cls::Close; break;
                case '\'': a.text = utf8(0x2032); break;                    // a prime
                default: break;
            }
        }
        out.push_back(std::move(a));
    }

    static Atom space(float em) {
        Atom a;
        a.kind = Atom::Kind::Space;
        a.em = em;
        return a;
    }

    static Atom glyph(char32_t cp, Cls cls) {
        Atom a;
        a.text = utf8(cp);
        a.cls = cls;
        return a;
    }

    void command(MathList& out) {
        const std::size_t at = i_++;
        if (i_ >= s_.size()) fail(at, "'\\' at the end");
        const char c = s_[i_];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter) {
            ++i_;
            switch (c) {
                case ',': out.push_back(space(1.0f / 6.0f)); return;
                case ':': case '>': out.push_back(space(2.0f / 9.0f)); return;
                case ';': out.push_back(space(5.0f / 18.0f)); return;
                case '!': out.push_back(space(-1.0f / 6.0f)); return;
                case ' ': out.push_back(space(1.0f / 3.0f)); return;
                case '{': out.push_back(glyph('{', Cls::Open)); return;
                case '}': out.push_back(glyph('}', Cls::Close)); return;
                case '_': case '%': case '#': case '$': case '&':
                    out.push_back(glyph(static_cast<char32_t>(c), Cls::Ord));
                    return;
                case '|': out.push_back(glyph(0x2016, Cls::Ord)); return;
                default: break;
            }
            fail(at, std::string("'\\") + c + "' is not supported");
        }
        std::size_t e = i_;
        while (e < s_.size() && ((s_[e] >= 'a' && s_[e] <= 'z') || (s_[e] >= 'A' && s_[e] <= 'Z'))) ++e;
        const std::string_view name = s_.substr(i_, e - i_);
        i_ = e;

        for (const Symbol& sym: kSymbols)
            if (sym.name == name) {
                out.push_back(glyph(sym.cp, sym.cls));
                return;
            }
        for (const SpaceCmd& sp: kSpaces)
            if (sp.name == name) {
                out.push_back(space(sp.em));
                return;
            }
        if (contains(kFunctions, name)) {
            Atom a;
            a.text = std::string(name);
            a.cls = Cls::Op;
            out.push_back(std::move(a));
            return;
        }
        if (name == "mathrm" || name == "mathit" || name == "mathdefault") {
            // Upright and italic draw alike until italics (step 32b).
            skip_spaces();
            if (i_ >= s_.size() || s_[i_] != '{') fail(at, "'\\" + std::string(name) + "' needs '{'");
            ++i_;
            Atom g;
            g.kind = Atom::Kind::Group;
            g.group = list(End::Brace);
            out.push_back(std::move(g));
            return;
        }
        if (name == "text" || name == "textrm") {
            skip_spaces();
            if (i_ >= s_.size() || s_[i_] != '{') fail(at, "'\\" + std::string(name) + "' needs '{'");
            out.push_back(text_group());
            return;
        }
        if (contains(kDeferred, name))
            fail(at, "'\\" + std::string(name) + "' is not supported yet");
        fail(at, "unknown command '\\" + std::string(name) + "'");
    }

    // \text{...}: text kept as typed, spaces included; `\{`, `\}`, `\$` and
    // friends are their character, and balanced braces nest.
    Atom text_group() {
        const std::size_t open = i_++;
        Atom a;
        int depth = 0;
        for (;;) {
            if (i_ >= s_.size() || s_[i_] == '$') fail(open, "'{' is never closed");
            const char c = s_[i_];
            if (c == '\\' && i_ + 1 < s_.size()
                && std::string_view("{}$_%#&").find(s_[i_ + 1]) != std::string_view::npos) {
                a.text += s_[i_ + 1];
                i_ += 2;
                continue;
            }
            if (c == '{') ++depth;
            if (c == '}' && depth-- == 0) {
                ++i_;
                break;
            }
            a.text += c;
            ++i_;
        }
        if (a.text.empty()) a.kind = Atom::Kind::Group;
        return a;
    }

    // TeX's rule: a binary operator with nothing to bind on one side is ordinary
    // (the unary minus).
    static void fix_binaries(MathList& l) {
        Atom* prev = nullptr;
        for (Atom& a: l) {
            if (a.kind == Atom::Kind::Space) continue;
            if (a.cls == Cls::Bin && (!prev || prev->cls == Cls::Bin || prev->cls == Cls::Op
                                      || prev->cls == Cls::Rel || prev->cls == Cls::Open
                                      || prev->cls == Cls::Punct))
                a.cls = Cls::Ord;
            if (prev && prev->cls == Cls::Bin
                && (a.cls == Cls::Rel || a.cls == Cls::Close || a.cls == Cls::Punct))
                prev->cls = Cls::Ord;
            prev = &a;
        }
        if (prev && prev->cls == Cls::Bin) prev->cls = Cls::Ord;
    }
};

std::size_t column_of(std::string_view s, std::size_t pos) {
    std::size_t col = 1;
    for (std::size_t k = 0; k < pos && k < s.size(); ++col)
        k += utf8_len(static_cast<unsigned char>(s[k]));
    return col;
}

Parsed parse(std::string_view s) {
    Parsed p;
    try {
        p.segments = Parser(s).line();
    } catch (const ParseFail& f) {
        p.segments.clear();
        p.error = MathError{column_of(s, f.pos), f.reason};
    }
    return p;
}

// ---- Layout -----------------------------------------------------------------

// Script sizes as fractions of the line's size: 0.7 (matplotlib's shrink
// factor) for a first-level script, 0.5 for every deeper one.
float scale_at(int level) {
    return level <= 0 ? 1.0f : level == 1 ? 0.7f : 0.5f;
}

// TeX's inter-atom spacing (The TeXbook, ch. 18), rows the left atom and
// columns the right one, in the order of Cls. 1 thin, 2 medium, 3 thick;
// negative = only at level 0 (no medium or thick space inside a script).
constexpr int kSpacing[7][7] = {
    //        Ord Op  Bin Rel Open Close Punct
    /*Ord*/   {0,  1, -2, -3,  0,  0,  0},
    /*Op*/    {1,  1,  0, -3,  0,  0,  0},
    /*Bin*/   {-2, -2, 0,  0, -2,  0,  0},
    /*Rel*/   {-3, -3, 0,  0, -3,  0,  0},
    /*Open*/  {0,  0,  0,  0,  0,  0,  0},
    /*Close*/ {0,  1, -2, -3,  0,  0,  0},
    /*Punct*/ {-1, -1, 0, -1, -1, -1, -1},
};

float spacing_em(Cls a, Cls b, int level) {
    const int v = kSpacing[static_cast<int>(a)][static_cast<int>(b)];
    if (v < 0 && level > 0) return 0.0f;
    switch (v < 0 ? -v : v) {
        case 1: return 1.0f / 6.0f;
        case 2: return 2.0f / 9.0f;
        case 3: return 5.0f / 18.0f;
        default: return 0.0f;
    }
}

// Script placement, in ems of the nucleus' size: a superscript's baseline up
// by kSupRaise, a subscript's down by kSubDrop (kSubDropBoth under a
// superscript as well), then kScriptSpace after the scripts.
constexpr float kSupRaise = 0.40f;
constexpr float kSubDrop = 0.18f;
constexpr float kSubDropBoth = 0.25f;
constexpr float kScriptSpace = 0.05f;

struct Box {
    std::vector<RichRun> runs;
    float width = 0.0f;
    float ascent = 0.0f, descent = 0.0f;   // descent <= 0
};

class Layout {
public:
    Layout(const std::string& font, float base) : font_(font), base_(base) {}

    Box text(const std::string& t, float size) const {
        Box b;
        const FontVMetrics vm = font_vmetrics(font_, size);
        b.ascent = vm.ascent;
        b.descent = vm.descent;
        if (t.empty()) return b;
        b.width = text_width(font_, size, t);
        b.runs.push_back({t, 0.0f, 0.0f, size, b.width});
        return b;
    }

    Box list(const MathList& l, int level) const {
        const float size = base_ * scale_at(level);
        Box out;
        bool any = false;
        Cls prev = Cls::Ord;
        for (const Atom& a: l) {
            if (a.kind == Atom::Kind::Space) {
                out.width += a.em * size;
                continue;
            }
            if (any) out.width += spacing_em(prev, a.cls, level) * size;
            const Box b = scripted(a, level);
            append(out, b, out.width, 0.0f);
            out.width += b.width;
            prev = a.cls;
            any = true;
        }
        return out;
    }

private:
    const std::string& font_;
    float base_;

    static void append(Box& into, const Box& b, float x, float dy) {
        for (RichRun r: b.runs) {
            r.x += x;
            r.dy += dy;
            into.runs.push_back(std::move(r));
        }
        into.ascent = std::max(into.ascent, b.ascent - dy);
        into.descent = std::min(into.descent, b.descent - dy);
    }

    Box nucleus(const Atom& a, int level) const {
        if (a.kind == Atom::Kind::Group) {
            Box b = list(a.group, level);
            // An empty group still stands on the line, for the scripts it carries.
            const FontVMetrics vm = font_vmetrics(font_, base_ * scale_at(level));
            b.ascent = std::max(b.ascent, vm.ascent);
            b.descent = std::min(b.descent, vm.descent);
            return b;
        }
        return text(a.text, base_ * scale_at(level));
    }

    Box scripted(const Atom& a, int level) const {
        Box n = nucleus(a, level);
        if (!a.has_sub && !a.has_sup) return n;
        const float size = base_ * scale_at(level);
        const FontVMetrics vm = font_vmetrics(font_, size);
        // A tall or deep nucleus (a group with scripts of its own) pushes its
        // scripts out by what it adds to the plain font's extent.
        const float extra_up = std::max(0.0f, n.ascent - vm.ascent);
        const float extra_down = std::max(0.0f, vm.descent - n.descent);
        const float x = n.width;
        float scripts_w = 0.0f;
        if (a.has_sup) {
            const Box s = list(a.sup, level + 1);
            append(n, s, x, -(kSupRaise * size + extra_up));
            scripts_w = std::max(scripts_w, s.width);
        }
        if (a.has_sub) {
            const Box s = list(a.sub, level + 1);
            append(n, s, x, (a.has_sup ? kSubDropBoth : kSubDrop) * size + extra_down);
            scripts_w = std::max(scripts_w, s.width);
        }
        n.width = x + scripts_w + kScriptSpace * size;
        return n;
    }
};

// Neighbouring runs at one size and baseline, with nothing between them,
// become one: fewer draw calls, and plainer SVG.
void merge_runs(std::vector<RichRun>& runs) {
    std::vector<RichRun> out;
    for (RichRun& r: runs) {
        if (!out.empty()) {
            RichRun& p = out.back();
            if (p.size == r.size && p.dy == r.dy && std::abs(p.x + p.width - r.x) < 1e-3f) {
                p.text += r.text;
                p.width += r.width;
                continue;
            }
        }
        out.push_back(std::move(r));
    }
    runs = std::move(out);
}

thread_local bool t_math_on = true;

} // namespace

bool has_math(std::string_view s) {
    std::size_t dollars = 0, escaped = 0;
    for (std::size_t k = 0; k < s.size(); ++k)
        if (s[k] == '$') {
            ++dollars;
            if (k > 0 && s[k - 1] == '\\') ++escaped;
        }
    const std::size_t n = dollars - escaped;
    return n > 0 && n % 2 == 0;
}

std::optional<MathError> math_error(std::string_view s) {
    if (!has_math(s)) return std::nullopt;
    return parse(s).error;
}

bool is_rich(std::string_view s) {
    return t_math_on && has_math(s) && !parse(s).error;
}

RichLine layout_rich(const std::string& font_path, float size, std::string_view s) {
    RichLine line;
    const FontVMetrics vm = font_vmetrics(font_path, size);
    line.vm = vm;
    Parsed p;
    if (t_math_on && has_math(s)) p = parse(s);
    if (p.segments.empty() || p.error) {
        line.width = text_width(font_path, size, s);
        if (!s.empty()) line.runs.push_back({std::string(s), 0.0f, 0.0f, size, line.width});
        return line;
    }

    const Layout lay(font_path, size);
    Box all;
    all.ascent = vm.ascent;
    all.descent = vm.descent;
    for (const Segment& seg: p.segments) {
        const Box b = seg.math ? lay.list(seg.list, 0) : lay.text(seg.text, size);
        for (RichRun r: b.runs) {
            r.x += all.width;
            all.runs.push_back(std::move(r));
        }
        all.width += b.width;
        all.ascent = std::max(all.ascent, b.ascent);
        all.descent = std::min(all.descent, b.descent);
    }
    merge_runs(all.runs);
    line.runs = std::move(all.runs);
    line.width = all.width;
    line.vm = {all.ascent, all.descent, all.ascent - all.descent};
    return line;
}

float label_width(const std::string& font_path, float size, std::string_view s) {
    if (!is_rich(s)) return text_width(font_path, size, s);
    return layout_rich(font_path, size, s).width;
}

FontVMetrics label_vmetrics(const std::string& font_path, float size, std::string_view s) {
    if (!is_rich(s)) return font_vmetrics(font_path, size);
    return layout_rich(font_path, size, s).vm;
}

bool mathtext_on() { return t_math_on; }

MathTextScope::MathTextScope(bool on) : prev_(t_math_on) { t_math_on = on; }
MathTextScope::~MathTextScope() { t_math_on = prev_; }

void warn_math(bool mathtext, std::string_view who, std::string_view s) {
    if (!mathtext || s.find('$') == std::string_view::npos) return;
    const bool multi = s.find('\n') != std::string_view::npos;
    std::size_t start = 0, line_no = 1;
    for (;; ++line_no) {
        const std::size_t nl = s.find('\n', start);
        const std::string_view line = s.substr(start, nl == std::string_view::npos ? s.npos : nl - start);
        if (const auto e = math_error(line)) {
            std::string msg(who);
            msg += ": math not parsed at ";
            if (multi) msg += "line " + std::to_string(line_no) + ", ";
            msg += "column " + std::to_string(e->column) + ": " + e->reason + "; drawn as written";
            emit_message(msg);
            return;
        }
        if (nl == std::string_view::npos) return;
        start = nl + 1;
    }
}

} // namespace sextant
