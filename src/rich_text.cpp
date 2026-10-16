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

// An accent drawn over its body: a glyph of the font's, or one of the wide ones
// drawn as a path across the body.
enum class Wide { None, Hat, Tilde, RightArrow, LeftArrow };

struct Atom {
    enum class Kind {
        Glyphs, Group, Space, Frac, Radical, Overline, Underline, Accent, Delimited, Sized
    } kind = Kind::Glyphs;
    Cls cls = Cls::Ord;
    std::string text;              // Glyphs
    bool italic = false;           // Glyphs
    float em = 0.0f;               // Space, in ems of the current size
    // Group; a Frac's numerator; a Radical's radicand; an Over/Underline's body.
    std::vector<Atom> group;
    std::vector<Atom> den;         // Frac
    bool shrink = true;            // Frac: parts one level down (\frac, \tfrac; not \dfrac)
    std::vector<Atom> index;       // Radical: \sqrt[n]
    bool has_index = false;
    std::string accent;            // Accent: the glyph (UTF-8), "" for a wide one
    float accent_scale = 1.0f;     // Accent: the glyph's size, of the body's
    Wide wide = Wide::None;        // Accent
    bool binom = false;            // Frac: \binom, no bar, in parentheses
    // Delimited (\left ... \middle ... \right): the parts between delimiters,
    // each a Group, and the delimiters around and between them (one more).
    // Sized (\big ...): its one delimiter and height in ems. A delimiter is its
    // character; 0 is `.`, none.
    std::vector<Atom> parts;
    std::vector<char32_t> delims;
    float delim_em = 0.0f;
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

// The first code point of `s` (0 for an empty or broken one).
char32_t first_cp(std::string_view s) {
    if (s.empty()) return 0;
    const auto b0 = static_cast<unsigned char>(s[0]);
    const std::size_t n = utf8_len(b0);
    if (n > s.size()) return 0;
    char32_t cp = n == 1 ? b0 : n == 2 ? (b0 & 0x1Fu) : n == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
    for (std::size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[k]) & 0x3Fu);
    return cp;
}

// The letters math sets in italic, as matplotlib's mathtext does: Latin
// (accented ones too) and lower-case Greek, not Greek capitals.
bool math_letter(char32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
    if (cp >= 0x00C0 && cp <= 0x024F) return cp != 0x00D7 && cp != 0x00F7;   // not the times or divide sign
    if (cp >= 0x03B1 && cp <= 0x03C9) return true;
    return cp == 0x03D1 || cp == 0x03D5 || cp == 0x03D6 || cp == 0x03F1 || cp == 0x03F5;
}

bool greek_capital(char32_t cp) { return cp >= 0x0391 && cp <= 0x03A9; }

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
    {"imath", 0x0131, Cls::Ord}, {"jmath", 0x0237, Cls::Ord},   // dotless, for accents
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

// Accents: the font's spacing accent (a small arrow for \vec), or a wide one
// drawn across its body.
struct AccentCmd {
    std::string_view name;
    char32_t cp;
    Wide wide;
    float scale;
};
constexpr AccentCmd kAccents[] = {
    {"hat", 0x02C6, Wide::None, 1.0f}, {"bar", 0x00AF, Wide::None, 1.0f},
    {"tilde", 0x02DC, Wide::None, 1.0f}, {"dot", 0x02D9, Wide::None, 1.0f},
    {"ddot", 0x00A8, Wide::None, 1.0f}, {"acute", 0x00B4, Wide::None, 1.0f},
    {"grave", 0x0060, Wide::None, 1.0f}, {"breve", 0x02D8, Wide::None, 1.0f},
    {"check", 0x02C7, Wide::None, 1.0f}, {"mathring", 0x02DA, Wide::None, 1.0f},
    {"vec", 0x2192, Wide::None, 0.6f},
    {"widehat", 0, Wide::Hat, 1.0f}, {"widetilde", 0, Wide::Tilde, 1.0f},
    {"overrightarrow", 0, Wide::RightArrow, 1.0f}, {"overleftarrow", 0, Wide::LeftArrow, 1.0f},
};

// Commands known from mathtext that this step does not draw yet.
constexpr std::string_view kDeferred[] = {
    "mathbf", "boldsymbol", "mathcal", "mathbb",
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
    // Right: up to a `\middle` or `\right` (left unconsumed), for `\left`.
    enum class End { Dollar, Brace, Bracket, Right };
    // The font a group sets: math's default (italic letters), \mathrm, \mathit.
    enum class Font { Math, Roman, Italic };

    std::string_view s_;
    std::size_t i_ = 0;
    Font font_ = Font::Math;

    // Whether a glyph atom's text draws italic in the current font.
    bool italic(std::string_view text) const {
        const char32_t cp = first_cp(text);
        switch (font_) {
            case Font::Math: return math_letter(cp);
            case Font::Italic: return math_letter(cp) || greek_capital(cp) || (cp >= '0' && cp <= '9');
            default: return false;
        }
    }

    [[noreturn]] void fail(std::size_t pos, std::string reason) const {
        throw ParseFail{pos, std::move(reason)};
    }

    void skip_spaces() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t')) ++i_;
    }

    [[noreturn]] void fail_unclosed(End end, std::size_t open) const {
        fail(open, end == End::Brace ? "'{' is never closed"
                   : end == End::Bracket ? "'[' is never closed"
                   : end == End::Right ? "'\\left' is never closed" : "'$' is never closed");
    }

    // The command name at i_ (which is on a backslash), or "" for a symbol one.
    std::string_view name_at(std::size_t at) const {
        std::size_t e = at + 1;
        while (e < s_.size() && ((s_[e] >= 'a' && s_[e] <= 'z') || (s_[e] >= 'A' && s_[e] <= 'Z'))) ++e;
        return s_.substr(at + 1, e - at - 1);
    }

    // Atoms up to the closing `$`, `}` or `]` (consumed), or for End::Right
    // up to a `\middle` or `\right` (not consumed); `open` is where the
    // construct began, for the error.
    MathList list(End end, std::size_t open = std::string_view::npos) {
        MathList out;
        if (open == std::string_view::npos) open = i_ - 1;
        for (;;) {
            if (i_ >= s_.size()) fail_unclosed(end, open);
            const char c = s_[i_];
            if (c == '\\' && end == End::Right
                && (name_at(i_) == "right" || name_at(i_) == "middle")) {
                break;
            }
            if (c == ' ' || c == '\t') {
                ++i_;   // TeX: a typed space does nothing in math
            } else if (c == '$') {
                if (end != End::Dollar) fail_unclosed(end, open);
                ++i_;
                break;
            } else if (c == '}') {
                if (end != End::Brace) fail(i_, "'}' without a '{'");
                ++i_;
                break;
            } else if (c == ']' && end == End::Bracket) {
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
        return one_atom();
    }

    // A command's argument: one atom or group, as TeX takes `\frac12`.
    MathList required_arg(std::size_t at, std::string_view name) {
        skip_spaces();
        if (i_ >= s_.size() || s_[i_] == '$' || s_[i_] == '}' || s_[i_] == '^' || s_[i_] == '_')
            fail(at, "'\\" + std::string(name) + "' is missing an argument");
        return one_atom();
    }

    // The atom at i_, a group's contents unwrapped.
    MathList one_atom() {
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
        a.italic = italic(a.text);
        out.push_back(std::move(a));
    }

    static Atom space(float em) {
        Atom a;
        a.kind = Atom::Kind::Space;
        a.em = em;
        return a;
    }

    Atom glyph(char32_t cp, Cls cls) const {
        Atom a;
        a.text = utf8(cp);
        a.cls = cls;
        a.italic = italic(a.text);
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

        if (name == "left") {
            Atom a;
            a.kind = Atom::Kind::Delimited;
            a.delims.push_back(delimiter(at, name));
            for (;;) {
                Atom part;
                part.kind = Atom::Kind::Group;
                part.group = list(End::Right, at);
                a.parts.push_back(std::move(part));
                const std::size_t cmd = i_;
                const std::string_view which = name_at(cmd);
                i_ = cmd + 1 + which.size();
                a.delims.push_back(delimiter(cmd, which));
                if (which == "right") break;
            }
            out.push_back(std::move(a));
            return;
        }
        if (name == "right" || name == "middle")
            fail(at, "'\\" + std::string(name) + "' without a '\\left'");
        if (const float em = big_em(name); em > 0.0f) {
            Atom a;
            a.kind = Atom::Kind::Sized;
            a.delim_em = em;
            const char last = name.back();
            a.cls = last == 'l' ? Cls::Open : last == 'r' ? Cls::Close : last == 'm' ? Cls::Rel : Cls::Ord;
            a.delims.push_back(delimiter(at, name));
            out.push_back(std::move(a));
            return;
        }
        if (name == "binom") {
            Atom a;
            a.kind = Atom::Kind::Frac;
            a.binom = true;
            a.group = required_arg(at, name);
            a.den = required_arg(at, name);
            out.push_back(std::move(a));
            return;
        }
        if (name == "frac" || name == "tfrac" || name == "dfrac") {
            Atom a;
            a.kind = Atom::Kind::Frac;
            a.shrink = name != "dfrac";
            a.group = required_arg(at, name);
            a.den = required_arg(at, name);
            out.push_back(std::move(a));
            return;
        }
        if (name == "overline" || name == "underline") {
            Atom a;
            a.kind = name == "overline" ? Atom::Kind::Overline : Atom::Kind::Underline;
            a.group = required_arg(at, name);
            out.push_back(std::move(a));
            return;
        }
        for (const AccentCmd& acc: kAccents)
            if (acc.name == name) {
                Atom a;
                a.kind = Atom::Kind::Accent;
                if (acc.cp) a.accent = utf8(acc.cp);
                a.accent_scale = acc.scale;
                a.wide = acc.wide;
                a.group = required_arg(at, name);
                out.push_back(std::move(a));
                return;
            }
        if (name == "sqrt") {
            skip_spaces();
            Atom a;
            a.kind = Atom::Kind::Radical;
            if (i_ < s_.size() && s_[i_] == '[') {
                ++i_;
                a.index = list(End::Bracket);
                a.has_index = true;
            } else if (i_ >= s_.size() || std::string_view("$}^_").find(s_[i_]) != std::string_view::npos) {
                // Nothing to go under: the sign alone, as before roots were drawn.
                out.push_back(glyph(0x221A, Cls::Ord));
                return;
            }
            a.group = required_arg(at, name);
            out.push_back(std::move(a));
            return;
        }

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
            // \mathdefault is matplotlib's text font: upright.
            skip_spaces();
            if (i_ >= s_.size() || s_[i_] != '{') fail(at, "'\\" + std::string(name) + "' needs '{'");
            ++i_;
            const Font outer = font_;
            font_ = name == "mathit" ? Font::Italic : Font::Roman;
            Atom g;
            g.kind = Atom::Kind::Group;
            g.group = list(End::Brace);
            font_ = outer;
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

    // The total height, in ems, of `\big` and its kin (`l`/`r`/`m` forms
    // included): amsmath's 1.2, 1.8, 2.4, 3.0 — plain TeX's 8.5…17.5 pt boxes,
    // measured as TeX sizes a delimiter, twice their reach from the math axis.
    // 0 for any other name.
    static float big_em(std::string_view name) {
        if (!name.empty() && (name.back() == 'l' || name.back() == 'r' || name.back() == 'm'))
            name.remove_suffix(1);
        if (name == "big") return 1.2f;
        if (name == "Big") return 1.8f;
        if (name == "bigg") return 2.4f;
        if (name == "Bigg") return 3.0f;
        return 0.0f;
    }

    // The delimiter after `\left`, `\middle`, `\right` or `\big…` (named `cmd`,
    // at `at`): its character, 0 for `.`.
    char32_t delimiter(std::size_t at, std::string_view cmd) {
        skip_spaces();
        const auto bad = [&] { fail(at, "'\\" + std::string(cmd) + "' needs a delimiter"); };
        if (i_ >= s_.size()) bad();
        const char c = s_[i_];
        if (std::string_view("()[]|/.<>").find(c) != std::string_view::npos) {
            ++i_;
            return c == '.' ? 0 : c == '<' ? 0x27E8 : c == '>' ? 0x27E9 : static_cast<char32_t>(c);
        }
        if (c != '\\' || i_ + 1 >= s_.size()) bad();
        const char n = s_[i_ + 1];
        if (n == '{' || n == '}' || n == '|') {
            i_ += 2;
            return n == '|' ? 0x2016 : static_cast<char32_t>(n);
        }
        const std::string_view name = name_at(i_);
        struct Named {
            std::string_view name;
            char32_t cp;
        };
        static constexpr Named kNamed[] = {
            {"lbrace", '{'}, {"rbrace", '}'}, {"vert", '|'}, {"Vert", 0x2016},
            {"langle", 0x27E8}, {"rangle", 0x27E9}, {"lfloor", 0x230A}, {"rfloor", 0x230B},
            {"lceil", 0x2308}, {"rceil", 0x2309}, {"backslash", '\\'},
        };
        for (const Named& d: kNamed)
            if (d.name == name) {
                i_ += 1 + name.size();
                return d.cp;
            }
        bad();
        return 0;
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
// A superscript after an italic letter moves right by this much, clear of the
// letter's slanted top (matplotlib's delta_slanted kern; TeX's italic
// correction). Its subscript stays at the letter's edge.
constexpr float kSupItalicKern = 0.06f;

// Fractions, roots and rules, in ems of the size they are set at: the rule
// thickness θ (matplotlib's, which hardcodes 0.75/12 for TrueType fonts), drawn
// never thinner than a pixel; 2θ between a fraction's bar and each part, 3θ
// over or under a body for its rule (TeX), TeX's null delimiter space on each
// side of a fraction.
constexpr float kRuleEm = 0.0625f;
constexpr float kMinRulePx = 1.0f;
constexpr float kFracGap = 2.0f;        // × θ
constexpr float kFracSide = 0.12f;
constexpr float kLineGap = 3.0f;        // × θ, over/underline and the root's bar
constexpr float kRootPad = 2.0f;        // × θ, each side of a radicand

struct Box {
    std::vector<RichRun> runs;
    std::vector<RichRule> rules;
    std::vector<RichPath> paths;
    float width = 0.0f;
    float ascent = 0.0f, descent = 0.0f;   // descent <= 0
    float ink_top = 0.0f, ink_bottom = 0.0f;   // y up, ink_bottom <= 0 <= ink_top
    // The ink's left and right, from the box's left end; left > right while
    // the box has no ink (an empty group, a space).
    float ink_left = 1e30f, ink_right = -1e30f;
    bool has_ink_x() const { return ink_left <= ink_right; }
    // Holds a run drawn back over another (an accent), which the SVG must place
    // absolutely; rules and paths make a line placed by themselves.
    bool placed = false;
};

// The outline of a polyline stroked with a width per segment, mitred at each
// joint (a bevel's midpoint where a mitre would reach past 2 widths), butt
// ends. y down. Used for the radical.
RichPath stroke_outline(const std::vector<RichPath::Pt>& c, const std::vector<float>& w) {
    using Pt = RichPath::Pt;
    const std::size_t m = c.size() - 1;
    std::vector<Pt> d(m), n(m);
    for (std::size_t i = 0; i < m; ++i) {
        const float dx = c[i + 1].x - c[i].x, dy = c[i + 1].y - c[i].y;
        const float l = std::max(std::hypot(dx, dy), 1e-6f);
        d[i] = {dx / l, dy / l};
        n[i] = {-d[i].y, d[i].x};
    }
    const auto side = [&](float sign) {
        std::vector<Pt> out;
        out.push_back({c[0].x + sign * n[0].x * w[0] * 0.5f, c[0].y + sign * n[0].y * w[0] * 0.5f});
        for (std::size_t j = 1; j < m; ++j) {
            const float h0 = sign * w[j - 1] * 0.5f, h1 = sign * w[j] * 0.5f;
            const Pt a{c[j].x + n[j - 1].x * h0, c[j].y + n[j - 1].y * h0};
            const Pt b{c[j].x + n[j].x * h1, c[j].y + n[j].y * h1};
            // a + d[j-1]·s = b + d[j]·t, solved for s.
            const float den = d[j - 1].x * d[j].y - d[j - 1].y * d[j].x;
            Pt p{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            if (std::abs(den) > 1e-6f) {
                const float s = ((b.x - a.x) * d[j].y - (b.y - a.y) * d[j].x) / den;
                const Pt q{a.x + d[j - 1].x * s, a.y + d[j - 1].y * s};
                if (std::hypot(q.x - c[j].x, q.y - c[j].y) <= 2.0f * std::max(w[j - 1], w[j])) p = q;
            }
            out.push_back(p);
        }
        const float h = sign * w[m - 1] * 0.5f;
        out.push_back({c[m].x + n[m - 1].x * h, c[m].y + n[m - 1].y * h});
        return out;
    };
    RichPath path;
    path.pts = side(1.0f);
    const std::vector<Pt> right = side(-1.0f);
    path.pts.insert(path.pts.end(), right.rbegin(), right.rend());
    return path;
}

class Layout {
public:
    Layout(const std::string& font, float base) : font_(font), base_(base) {}

    Box text(const std::string& t, float size, bool italic = false) const {
        Box b;
        const FontVMetrics vm = font_vmetrics(font_, size);
        b.ascent = vm.ascent;
        b.descent = vm.descent;
        if (t.empty()) return b;
        b.width = italic ? text_width_italic(font_, size, t) : text_width(font_, size, t);
        b.runs.push_back({t, 0.0f, 0.0f, size, b.width, italic});
        const InkBox ink = ink_box(font_, size, t, italic);
        b.ink_top = std::max(0.0f, ink.top);
        b.ink_bottom = std::min(0.0f, ink.bottom);
        if (ink.right > ink.left) {
            b.ink_left = ink.left;
            b.ink_right = ink.right;
        }
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
        for (RichRule r: b.rules) {
            r.x += x;
            r.y += dy;
            into.rules.push_back(r);
        }
        for (RichPath p: b.paths) {
            for (RichPath::Pt& q: p.pts) {
                q.x += x;
                q.y += dy;
            }
            into.paths.push_back(std::move(p));
        }
        into.ascent = std::max(into.ascent, b.ascent - dy);
        into.descent = std::min(into.descent, b.descent - dy);
        into.ink_top = std::max(into.ink_top, b.ink_top - dy);
        into.ink_bottom = std::min(into.ink_bottom, b.ink_bottom - dy);
        if (b.has_ink_x()) cover(into, b.ink_left + x, b.ink_right + x);
        into.placed = into.placed || b.placed;
    }

    // Grows `b`'s extents to reach `top` and `bottom` (y up).
    static void reach(Box& b, float top, float bottom) {
        b.ascent = std::max(b.ascent, top);
        b.descent = std::min(b.descent, bottom);
        b.ink_top = std::max(b.ink_top, top);
        b.ink_bottom = std::min(b.ink_bottom, bottom);
    }

    // Grows `b`'s ink to span x0..x1.
    static void cover(Box& b, float x0, float x1) {
        b.ink_left = std::min(b.ink_left, x0);
        b.ink_right = std::max(b.ink_right, x1);
    }

    // `b`'s extents grown over a drawn path's points (y down).
    static void reach_path(Box& b, const RichPath& p) {
        for (const RichPath::Pt& q: p.pts) {
            reach(b, -q.y, -q.y);
            cover(b, q.x, q.x);
        }
    }

    // The math axis at `size`, y up: the middle of `=`, where a fraction's bar
    // sits.
    float axis(float size) const {
        const InkBox eq = ink_box(font_, size, "=");
        return eq.top > eq.bottom ? (eq.top + eq.bottom) * 0.5f : 0.25f * size;
    }

    // The weight of a stroke drawn to match the font's: its `|`.
    float stem(float size) const {
        const InkBox bar = ink_box(font_, size, "|");
        const float w = bar.right > bar.left ? bar.right - bar.left : 0.08f * size;
        return std::clamp(w, kMinRulePx, 0.15f * size + kMinRulePx);
    }

    Box fraction(const Atom& a, int level) const {
        const float size = base_ * scale_at(level);
        const int part = a.shrink ? level + 1 : level;
        const Box num = list(a.group, part);
        const Box den = list(a.den, part);
        const float theta = kRuleEm * size;
        const float bar = a.binom ? 0.0f : std::max(theta, kMinRulePx);
        const float ax = axis(size);
        const float side = a.binom ? 0.0f : kFracSide * size;
        const float w = std::max(num.width, den.width);
        Box b;
        // Baselines (y up) that leave 2θ between the bar and each part's ink
        // (the axis for \binom, which has none).
        const float num_base = ax + bar * 0.5f + kFracGap * theta - num.ink_bottom;
        const float den_base = ax - bar * 0.5f - kFracGap * theta - den.ink_top;
        append(b, num, side + (w - num.width) * 0.5f, -num_base);
        append(b, den, side + (w - den.width) * 0.5f, -den_base);
        b.width = w + 2.0f * side;
        if (a.binom) return enclose(b, '(', ')', size);
        b.rules.push_back({side, -(ax + bar * 0.5f), w, bar});
        reach(b, ax + bar * 0.5f, ax - bar * 0.5f);
        cover(b, side, side + w);
        return b;
    }

    // ---- Delimiters ----------------------------------------------------------

    // The height a delimiter around `parts` needs, at `size`: TeX's rule, from
    // twice their furthest ink from the math axis, h: max(0.901·h, h − 0.5 em)
    // (\delimiterfactor, \delimitershortfall).
    float delimiter_height(const std::vector<const Box*>& parts, float size) const {
        const float ax = axis(size);
        float reach_ = 0.0f;
        for (const Box* p: parts) reach_ = std::max({reach_, p->ink_top - ax, ax - p->ink_bottom});
        const float h = 2.0f * reach_;
        return std::max(0.901f * h, h - 0.5f * size);
    }

    // `content` between two delimiters sized to it.
    Box enclose(const Box& content, char32_t l, char32_t r, float size) const {
        const float h = delimiter_height({&content}, size);
        Box b = delimiter(l, h, size);
        append(b, content, b.width, 0.0f);
        b.width += content.width;
        const Box rb = delimiter(r, h, size);
        append(b, rb, b.width, 0.0f);
        b.width += rb.width;
        return b;
    }

    // One delimiter (0 = none, TeX's null delimiter space) at least `target`
    // tall, centred on the math axis: the font's glyph while that is tall
    // enough, else drawn to the height (filled paths and rules, the font's stem
    // weight), so it stays crisp at any size.
    Box delimiter(char32_t cp, float target, float size) const {
        using Pt = RichPath::Pt;
        Box b;
        if (cp == 0) {
            b.width = kFracSide * size;
            return b;
        }
        const float ax = axis(size);
        const std::string glyph = utf8(cp);
        const InkBox gi = ink_box(font_, size, glyph);
        if (gi.top > gi.bottom && target <= gi.top - gi.bottom + 1e-3f) {
            const Box g = text(glyph, size);
            append(b, g, 0.0f, -(ax - (gi.top + gi.bottom) * 0.5f));
            b.width = g.width;
            return b;
        }

        const float theta = kRuleEm * size;
        const float bar = std::max(theta, kMinRulePx);
        const float s = stem(size);
        const float pad = 0.08f * size;
        const float top = ax + target * 0.5f, bot = ax - target * 0.5f;
        const bool closing = cp == ')' || cp == ']' || cp == '}' || cp == 0x27E9 || cp == 0x230B
                             || cp == 0x2309 || cp == '\\';
        // Each shape is built opening (left-hand), y up, then mirrored for a
        // closing one; `w` is its box width.
        std::vector<RichRule> rules;   // x, y (top edge, y up), w, h
        std::vector<RichPath> paths;
        float w = 0.0f;
        const auto up = [](float x, float y) { return Pt{x, y}; };
        switch (cp) {
            case '(': case ')': {
                const float te = std::max(bar, 0.4f * s);
                const float bulge = std::min(0.10f * size + 0.06f * target, 0.45f * size);
                // A parabola from each end, `hook` tall, straight between: about a
                // circular arc while short, and as it grows the straight middle of
                // TeX's built-up parens.
                const float hook = std::min(0.5f * target, 0.75f * size);
                const float xin = pad + s * 0.5f;
                constexpr int kSteps = 32;
                std::vector<Pt> spine;
                std::vector<float> widths;
                for (int i = 0; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / kSteps;
                    const float d = std::min(t, 1.0f - t) * target;   // from the nearer end
                    const float f = d >= hook ? 0.0f : (1.0f - d / hook) * (1.0f - d / hook);
                    spine.push_back(up(xin + bulge * f, top - target * t));
                    if (i < kSteps) {
                        const float tm = (static_cast<float>(i) + 0.5f) / kSteps;
                        widths.push_back(te + (s - te) * std::sin(3.14159265f * tm));
                    }
                }
                paths.push_back(stroke_outline(spine, widths));
                w = xin + bulge + te * 0.5f + pad;
                break;
            }
            case '[': case ']': case 0x230A: case 0x230B: case 0x2308: case 0x2309: {
                const float arm = 0.22f * size + 0.03f * target;
                const float ta = std::max(bar, 0.5f * s);
                rules.push_back({pad, top, s, target});
                const bool floor_ = cp == 0x230A || cp == 0x230B, ceil_ = cp == 0x2308 || cp == 0x2309;
                if (!floor_) rules.push_back({pad, top, arm, ta});
                if (!ceil_) rules.push_back({pad, bot + ta, arm, ta});
                w = pad + arm + 0.5f * pad;
                break;
            }
            case '|': case 0x2016: {
                const float gap = 0.1f * size;
                rules.push_back({pad, top, s, target});
                w = pad + s + pad;
                if (cp == 0x2016) {
                    rules.push_back({pad + s + gap, top, s, target});
                    w += s + gap;
                }
                break;
            }
            case 0x27E8: case 0x27E9: {
                const float wa = std::max(bar, 0.6f * s);
                const float dx = std::min(0.10f * size + 0.10f * target, 0.5f * size);
                const float x0 = pad + wa * 0.5f;
                paths.push_back(stroke_outline({up(x0 + dx, top), up(x0, ax), up(x0 + dx, bot)}, {wa, wa}));
                w = x0 + dx + wa * 0.5f + pad;
                break;
            }
            case '/': case '\\': {
                const float wa = std::max(bar, 0.7f * s);
                const float dx = std::min(0.3f * target, 0.8f * size);
                const float x0 = pad + wa * 0.5f;
                paths.push_back(stroke_outline({up(x0, bot), up(x0 + dx, top)}, {wa}));
                w = x0 + dx + wa * 0.5f + pad;
                break;
            }
            default: {   // '{', '}': hooks into a stem, a pointed cusp on the axis
                const float te = std::max(bar, 0.4f * s);
                const float q = std::min(0.18f * size, target * 0.22f);   // each hook's height
                const float xl = pad, xm = xl + 0.12f * size + 0.02f * target, xr = xm + 0.16f * size;
                std::vector<Pt> spine{up(xr, top)};
                std::vector<float> widths;
                const auto cubic = [&](Pt p0, Pt p1, Pt p2, Pt p3, float w0, float w1) {
                    constexpr int n = 10;
                    for (int i = 1; i <= n; ++i) {
                        const float t = static_cast<float>(i) / n, u = 1.0f - t;
                        spine.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                                         u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
                        widths.push_back(w0 + (w1 - w0) * (static_cast<float>(i) - 0.5f) / n);
                    }
                };
                cubic(up(xr, top), up(xm, top), up(xm, top), up(xm, top - q), te, s);
                spine.push_back(up(xm, ax + q));
                widths.push_back(s);
                cubic(up(xm, ax + q), up(xm, ax), up(xm, ax), up(xl, ax), s, te);
                cubic(up(xl, ax), up(xm, ax), up(xm, ax), up(xm, ax - q), te, s);
                spine.push_back(up(xm, bot + q));
                widths.push_back(s);
                cubic(up(xm, bot + q), up(xm, bot), up(xm, bot), up(xr, bot), s, te);
                paths.push_back(stroke_outline(spine, widths));
                w = xr + te * 0.5f + pad;
                break;
            }
        }
        // Into the box, y down, mirrored for a closing delimiter.
        const auto mx = [&](float x) { return closing ? w - x : x; };
        for (RichRule r: rules) {
            const float x0 = closing ? w - r.x - r.w : r.x;
            b.rules.push_back({x0, -r.y, r.w, r.h});
        }
        for (RichPath p: paths) {
            for (Pt& q: p.pts) q = {mx(q.x), -q.y};
            reach_path(b, p);
            b.paths.push_back(std::move(p));
        }
        reach(b, top, bot);
        cover(b, 0.0f, w);
        b.width = w;
        return b;
    }

    Box delimited(const Atom& a, int level) const {
        const float size = base_ * scale_at(level);
        std::vector<Box> parts;
        for (const Atom& p: a.parts) parts.push_back(list(p.group, level));
        std::vector<const Box*> ptrs;
        for (const Box& p: parts) ptrs.push_back(&p);
        const float h = delimiter_height(ptrs, size);
        Box b;
        for (std::size_t i = 0; i < a.delims.size(); ++i) {
            const Box d = delimiter(a.delims[i], h, size);
            append(b, d, b.width, 0.0f);
            b.width += d.width;
            if (i < parts.size()) {
                append(b, parts[i], b.width, 0.0f);
                b.width += parts[i].width;
            }
        }
        return b;
    }

    Box over_under(const Atom& a, int level) const {
        const float size = base_ * scale_at(level);
        Box b = list(a.group, level);
        const float theta = kRuleEm * size;
        const float bar = std::max(theta, kMinRulePx);
        if (a.kind == Atom::Kind::Overline) {
            const float bottom = b.ink_top + kLineGap * theta;
            b.rules.push_back({0.0f, -(bottom + bar), b.width, bar});
            reach(b, bottom + bar + theta, 0.0f);
        } else {
            const float top = b.ink_bottom - kLineGap * theta;
            b.rules.push_back({0.0f, -top, b.width, bar});
            reach(b, 0.0f, top - bar - theta);
        }
        cover(b, 0.0f, b.width);
        return b;
    }

    // A radical: the sign drawn as one outline with its bar (TeX's shape: a
    // thin tick, a thick down-stroke, a thin rise into the bar), so the join is
    // exact at any height; the index, if any, half size in the sign's notch.
    Box radical(const Atom& a, int level) const {
        using Pt = RichPath::Pt;
        const float size = base_ * scale_at(level);
        const Box body = list(a.group, level);
        const float theta = kRuleEm * size;
        const float bar = std::max(theta, kMinRulePx);
        const float thick = stem(size);
        const float top = body.ink_top + kLineGap * theta + bar;      // y up
        const float foot = std::min(body.ink_bottom, 0.0f) - theta;
        const float h = top - foot;
        const float tick_y = foot + std::min(0.5f * h, 0.42f * size);
        // x positions; the index may push everything right.
        const float x0 = 0.03f * size;
        const float x1 = x0 + 0.10f * size;
        const float xf = x1 + 0.17f * size;
        const float xc = xf + 0.20f * size + 0.06f * h;
        float shift = 0.0f;
        Box ib;
        float index_base = 0.0f;
        if (a.has_index) {
            ib = list(a.index, level + 2);
            index_base = foot + 0.6f * h;
            shift = std::max(0.0f, ib.width - (x1 + 0.05f * size));
        }
        const float bx = shift + xc + kRootPad * theta;
        const float end = bx + body.width + kRootPad * theta;
        Box b;
        if (a.has_index) append(b, ib, shift + x1 + 0.05f * size - ib.width, -index_base);
        append(b, body, bx, 0.0f);
        // y down from here on.
        const std::vector<Pt> spine = {
            {shift + x0, -tick_y},
            {shift + x1, -(tick_y + 0.05f * size)},
            {shift + xf, -foot},
            {shift + xc, -(top - bar * 0.5f)},
            {end, -(top - bar * 0.5f)},
        };
        b.paths.push_back(stroke_outline(spine, {bar, thick, bar, bar}));
        reach(b, top, foot);
        cover(b, shift + x0, end);
        b.width = end;
        return b;
    }

    // An accent over its body (TeX's and matplotlib's): centred on the body's
    // ink, 2θ clear of it; over one italic letter, moved right by half the slant
    // of its height, to sit over the letter's top. The body keeps its width.
    Box accent(const Atom& a, int level) const {
        using Pt = RichPath::Pt;
        const float size = base_ * scale_at(level);
        Box b = list(a.group, level);
        const float theta = kRuleEm * size;
        const float bar = std::max(theta, kMinRulePx);
        float x0 = 0.0f, x1 = b.width;
        if (b.has_ink_x()) {
            x0 = b.ink_left;
            x1 = b.ink_right;
        }
        const bool letter = a.group.size() == 1 && a.group[0].kind == Atom::Kind::Glyphs && a.group[0].italic
                            && !a.group[0].has_sub && !a.group[0].has_sup;
        const float skew = letter ? kSyntheticSlant * b.ink_top * 0.5f : 0.0f;
        const float mid = (x0 + x1) * 0.5f + skew;
        const float lift = b.ink_top + kFracGap * theta;   // the accent's ink bottom, y up

        if (a.wide == Wide::None) {
            const float asize = size * a.accent_scale;
            const InkBox ink = ink_box(font_, asize, a.accent);
            if (ink.right <= ink.left) return b;
            Box acc = text(a.accent, asize);
            // Its extent is its ink: an accent reserves no font ascent above it.
            acc.ascent = ink.top;
            acc.descent = ink.bottom;
            append(b, acc, mid - (ink.left + ink.right) * 0.5f, -(lift - ink.bottom));
            b.placed = true;
            return b;
        }

        // Wide accents span the ink (half an em at least), centred as above.
        const float half = std::max(x1 - x0, 0.5f * size) * 0.5f;
        const float l = mid - half, r = mid + half, w = r - l;
        const float weight = std::max(bar, 0.6f * stem(size));
        switch (a.wide) {
            case Wide::Hat: {
                const float h = std::clamp(0.12f * size + 0.05f * w, 0.0f, 0.3f * size);
                const float y = -(lift + weight * 0.5f);
                b.paths.push_back(stroke_outline({{l, y}, {mid, y - h}, {r, y}}, {bar, weight}));
                break;
            }
            case Wide::Tilde: {
                // Up, down, up: h/2·(1 − cos 3πt), heavier on the slopes.
                const float h = std::clamp(0.08f * size + 0.03f * w, 0.0f, 0.2f * size);
                const float y = -(lift + weight * 0.5f);
                constexpr int kSteps = 24;
                std::vector<Pt> spine;
                std::vector<float> widths;
                for (int k = 0; k <= kSteps; ++k) {
                    const float t = static_cast<float>(k) / kSteps;
                    spine.push_back({l + w * t, y - h * 0.5f * (1.0f - std::cos(3.0f * 3.14159265f * t))});
                    if (k < kSteps) {
                        const float tm = (static_cast<float>(k) + 0.5f) / kSteps;
                        widths.push_back(bar + (weight - bar) * std::abs(std::sin(3.0f * 3.14159265f * tm)));
                    }
                }
                b.paths.push_back(stroke_outline(spine, widths));
                break;
            }
            default: {
                // A shaft with an open head: barbs `head` long and wide.
                const float head = 0.12f * size;
                const float yc = -(lift + head);
                const bool right = a.wide == Wide::RightArrow;
                const float tip = right ? r : l, back = right ? r - 2.0f * head : l + 2.0f * head;
                b.paths.push_back(stroke_outline({{right ? l : r, yc}, {tip, yc}}, {bar}));
                b.paths.push_back(stroke_outline({{back, yc - head}, {tip, yc}, {back, yc + head}}, {weight, weight}));
                break;
            }
        }
        for (const RichPath& p: b.paths) reach_path(b, p);
        return b;
    }

    Box nucleus(const Atom& a, int level) const {
        switch (a.kind) {
            case Atom::Kind::Accent: return accent(a, level);
            case Atom::Kind::Delimited: return delimited(a, level);
            case Atom::Kind::Sized: {
                const float size = base_ * scale_at(level);
                return delimiter(a.delims.front(), a.delim_em * size, size);
            }
            case Atom::Kind::Group: {
                Box b = list(a.group, level);
                // An empty group still stands on the line, for the scripts it carries.
                const FontVMetrics vm = font_vmetrics(font_, base_ * scale_at(level));
                b.ascent = std::max(b.ascent, vm.ascent);
                b.descent = std::min(b.descent, vm.descent);
                return b;
            }
            case Atom::Kind::Frac: return fraction(a, level);
            case Atom::Kind::Radical: return radical(a, level);
            case Atom::Kind::Overline:
            case Atom::Kind::Underline: return over_under(a, level);
            default: return text(a.text, base_ * scale_at(level), a.italic);
        }
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
        const bool slanted = a.kind == Atom::Kind::Glyphs && a.italic;
        float scripts_end = x;
        if (a.has_sup) {
            const Box s = list(a.sup, level + 1);
            const float sx = x + (slanted ? kSupItalicKern * size : 0.0f);
            append(n, s, sx, -(kSupRaise * size + extra_up));
            scripts_end = std::max(scripts_end, sx + s.width);
        }
        if (a.has_sub) {
            const Box s = list(a.sub, level + 1);
            append(n, s, x, (a.has_sup ? kSubDropBoth : kSubDrop) * size + extra_down);
            scripts_end = std::max(scripts_end, x + s.width);
        }
        n.width = scripts_end + kScriptSpace * size;
        return n;
    }
};

// Neighbouring runs at one size, baseline and slant, with nothing between
// them, become one: fewer draw calls, and plainer SVG.
void merge_runs(std::vector<RichRun>& runs) {
    std::vector<RichRun> out;
    for (RichRun& r: runs) {
        if (!out.empty()) {
            RichRun& p = out.back();
            if (p.size == r.size && p.dy == r.dy && p.italic == r.italic
                && std::abs(p.x + p.width - r.x) < 1e-3f) {
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
    line.ink_top = vm.ascent;
    line.ink_bottom = vm.descent;
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
        for (RichRule r: b.rules) {
            r.x += all.width;
            all.rules.push_back(r);
        }
        for (RichPath path: b.paths) {
            for (RichPath::Pt& q: path.pts) q.x += all.width;
            all.paths.push_back(std::move(path));
        }
        all.width += b.width;
        all.ascent = std::max(all.ascent, b.ascent);
        all.descent = std::min(all.descent, b.descent);
        all.ink_top = std::max(all.ink_top, b.ink_top);
        all.ink_bottom = std::min(all.ink_bottom, b.ink_bottom);
        all.placed = all.placed || b.placed;
    }
    merge_runs(all.runs);
    line.runs = std::move(all.runs);
    line.rules = std::move(all.rules);
    line.paths = std::move(all.paths);
    line.placed = all.placed || !line.rules.empty() || !line.paths.empty();
    line.width = all.width;
    line.vm = {all.ascent, all.descent, all.ascent - all.descent};
    line.ink_top = all.ink_top;
    line.ink_bottom = all.ink_bottom;
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
