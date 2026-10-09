// Rich text (v1.1 step 32a): the mathtext subset's parser and layout, the
// plain-text guarantee, every site drawing and measuring through the seam
// (PNG pixels and SVG tspans), FigureOptions::mathtext, and the warning for
// math that does not parse. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "rich_text.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr float kSize = 20.0f;

        bool close(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        float w(std::string_view s, float size = kSize) { return text_width("", size, s); }

        // The plot frame: the first axes-background rect in an SVG.
        bool frame_of(const std::string& svg, PlotRect& out) {
            const std::size_t at = svg.find("\" fill=\"white\"/>");
            if (at == std::string::npos) return false;
            const std::size_t start = svg.rfind("<rect x=\"", at);
            if (start == std::string::npos) return false;
            return std::sscanf(svg.c_str() + start, "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                               &out.x, &out.y, &out.w, &out.h) == 4;
        }

        // Rows and columns holding dark ink.
        struct Ink { int rows = 0, cols = 0, top = 1 << 30, bottom = -1; };
        Ink ink_of(const RgbaImage& img) {
            Ink k;
            std::vector<bool> col(static_cast<std::size_t>(img.width), false);
            for (int y = 0; y < img.height; ++y) {
                bool row = false;
                for (int x = 0; x < img.width; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
                    if (img.pixels[i] < 100 && img.pixels[i + 1] < 100 && img.pixels[i + 2] < 100) {
                        row = true;
                        col[static_cast<std::size_t>(x)] = true;
                    }
                }
                if (row) {
                    ++k.rows;
                    k.top = std::min(k.top, y);
                    k.bottom = std::max(k.bottom, y);
                }
            }
            for (bool c: col) k.cols += c;
            return k;
        }

        // One text, alone on a bare white figure.
        RgbaImage text_alone(const std::string& s, bool mathtext = true) {
            FigureOptions o;
            o.width = 300;
            o.height = 160;
            o.background = Color::White;
            o.mathtext = mathtext;
            auto fig = Figure::create(o);
            auto ax = fig->add_subplot(1, 1, 1);
            AxesStyle st;
            st.show_xticks = st.show_yticks = false;
            st.spine_bottom = st.spine_left = st.spine_top = st.spine_right = false;
            ax->set_axes_style(st);
            TextOptions to;
            to.fontsize = 40.0f;
            to.color = Color::Black;
            to.ha = HAlign::Center;
            to.va = VAlign::Center;
            ax->text(s, Pos::fraction(0.5), Pos::fraction(0.5), to);
            return fig->render_rgba();
        }

        std::string svg_titled(const std::string& title, bool mathtext = true) {
            FigureOptions o;
            o.mathtext = mathtext;
            auto fig = Figure::create(o);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});
            ax->set_title(title);
            return fig->render_svg().svg;
        }

        // Messages reaching the handler while it is installed.
        struct Captured {
            std::vector<std::string> msgs;
            Captured() {
                Figure::set_message_handler([this](std::string_view s) { msgs.emplace_back(s); });
            }
            ~Captured() { Figure::set_message_handler(nullptr); }
        };
    } // namespace

    void test_rich_text() {
        std::printf("\n[rich text: mathtext subset (step 32a)]\n");

        // --- Which strings have math (matplotlib's rule) ----------------------
        check(!has_math("Price ($)"), "one '$' is plain");
        check(has_math("$x$") && has_math("$5 to $10"), "an even count of '$' is math");
        check(!has_math("\\$5 and \\$6"), "escaped dollars do not count");
        check(!has_math("no math here"), "no '$' is plain");

        // --- Parse errors, with their columns ---------------------------------
        auto err = [](std::string_view s) { return math_error(s); };
        check(!err("$x^2$") && !err("$\\alpha_{i,j}^{2}$") && !err("plain"), "valid math has no error");
        {
            const auto e = err("$\\alp$");
            check(e && e->column == 2 && e->reason.find("unknown command '\\alp'") != std::string::npos,
                  "unknown command, at its backslash");
            const auto f = err("$x^2^3$");
            check(f && f->column == 5 && f->reason == "double superscript", "double superscript, at the second '^'");
            check(err("${x$") && err("${x$")->reason == "'{' is never closed", "an unclosed '{'");
            check(err("$x}$") && err("$x}$")->reason == "'}' without a '{'", "a stray '}'");
            check(err("$x^$") && err("$x^$")->reason.find("nothing to apply") != std::string::npos,
                  "a script with no argument");
            const auto g = err("$\\frac{a}{b}$");
            check(g && g->reason.find("not supported yet") != std::string::npos, "a deferred command says so");
            const auto h = err("ab $\\u$");
            check(h && h->column == 5, "columns count code points from the string's start");
        }

        // --- Plain text measures exactly as before ----------------------------
        for (const char* s: {"Price ($)", "Temperature [K]", "a$b", ""}) {
            const RichLine l = layout_rich("", kSize, s);
            check(l.width == w(s) && label_width("", kSize, s) == w(s), std::string("plain width unchanged: ") + s);
            check(l.vm.ascent == font_vmetrics("", kSize).ascent && l.vm.descent == font_vmetrics("", kSize).descent,
                  std::string("plain extent unchanged: ") + s);
        }
        check(!is_rich("$\\alp$") && label_width("", kSize, "$\\alp$") == w("$\\alp$"),
              "malformed math measures as written");

        // --- Scripts ----------------------------------------------------------
        {
            const RichLine l = layout_rich("", kSize, "$x^2$");
            check(l.runs.size() == 2, "x^2: two runs");
            if (l.runs.size() == 2) {
                const RichRun& b = l.runs[0];
                const RichRun& s = l.runs[1];
                check(b.text == "x" && b.size == kSize && b.dy == 0.0f, "x^2: the base on the baseline");
                check(s.text == "2" && close(s.size, kSize * 0.7f), "x^2: the script at 0.7 of the size");
                check(close(s.dy, -0.40f * kSize) && close(s.x, w("x")), "x^2: raised 0.4 em, right after the base");
                check(close(l.width, w("x") + w("2", kSize * 0.7f) + 0.05f * kSize), "x^2: width, script space included");
            }
            check(l.vm.ascent > font_vmetrics("", kSize).ascent, "x^2: reaches above the plain font");
            check(l.vm.descent == font_vmetrics("", kSize).descent, "x^2: no deeper than the plain font");

            const RichLine sub = layout_rich("", kSize, "$x_i$");
            check(sub.runs.size() == 2 && close(sub.runs[1].dy, 0.18f * kSize), "x_i: lowered 0.18 em");
            check(sub.vm.descent < font_vmetrics("", kSize).descent, "x_i: reaches below the plain font");

            const RichLine both = layout_rich("", kSize, "$x_i^2$");
            check(both.runs.size() == 3 && close(both.runs[1].x, both.runs[2].x),
                  "x_i^2: both scripts stacked at one x");
            if (both.runs.size() == 3) {
                const float sup_dy = both.runs[1].text == "2" ? both.runs[1].dy : both.runs[2].dy;
                const float sub_dy = both.runs[1].text == "i" ? both.runs[1].dy : both.runs[2].dy;
                check(close(sup_dy, -0.40f * kSize) && close(sub_dy, 0.25f * kSize),
                      "x_i^2: the subscript drops further under a superscript");
            }

            const RichLine nest = layout_rich("", kSize, "$e^{x^2}$");
            check(nest.runs.size() == 3 && close(nest.runs[2].size, kSize * 0.5f), "a second level is half size");
            const RichLine deep = layout_rich("", kSize, "$e^{x^{y^z}}$");
            check(!deep.runs.empty() && close(deep.runs.back().size, kSize * 0.5f), "deeper levels stay at half size");
            if (nest.runs.size() == 3)
                check(nest.runs[2].dy < nest.runs[1].dy, "a nested superscript sits higher");
            check(!layout_rich("", kSize, "$^2$").runs.empty(), "a script with no base still draws");
        }

        // --- Symbols, spacing, text mode --------------------------------------
        {
            check(layout_rich("", kSize, "$\\alpha$").runs.size() == 1
                  && layout_rich("", kSize, "$\\alpha$").runs[0].text == "\xCE\xB1", "\\alpha is U+03B1");
            check(layout_rich("", kSize, "$\\nabla$").runs[0].text == "\xE2\x88\x87", "\\nabla is U+2207");
            check(layout_rich("", kSize, "$-1$").runs[0].text == "\xE2\x88\x92" "1", "'-' is the minus sign");
            check(layout_rich("", kSize, "$a b$").width == layout_rich("", kSize, "$ab$").width,
                  "a typed space does nothing in math");
            check(close(layout_rich("", kSize, "$a=b$").width, layout_rich("", kSize, "$a = b$").width),
                  "spaces around '=' are TeX's, not typed");

            // a + b: medium spaces on both sides of a binary operator...
            const RichLine bin = layout_rich("", kSize, "$a+b$");
            check(close(bin.width, w("a") + w("+") + w("b") + 2.0f * (2.0f / 9.0f) * kSize, 0.01f),
                  "a+b: a medium space each side of '+'");
            // ...but not on a unary one.
            check(close(layout_rich("", kSize, "$+b$").width, w("+") + w("b"), 0.01f), "+b: a unary '+' has none");
            const RichLine rel = layout_rich("", kSize, "$a=b$");
            check(close(rel.width, w("a") + w("=") + w("b") + 2.0f * (5.0f / 18.0f) * kSize, 0.01f),
                  "a=b: a thick space each side of '='");
            const float s7 = kSize * 0.7f;
            const RichLine sc = layout_rich("", kSize, "$x^{a+b}$");
            check(close(sc.width, w("x") + w("a", s7) + w("+", s7) + w("b", s7) + 0.05f * kSize, 0.01f),
                  "no medium space inside a script");
            check(close(layout_rich("", kSize, "$a\\,b$").width, w("a") + w("b") + kSize / 6.0f, 0.01f),
                  "\\, is a thin space");
            check(close(layout_rich("", kSize, "$a\\quad b$").width, w("a") + w("b") + kSize, 0.01f),
                  "\\quad is an em");
            check(close(layout_rich("", kSize, "$\\sin x$").width, w("sin") + w("x") + kSize / 6.0f, 0.01f),
                  "\\sin: upright, a thin space before its argument");

            const RichLine mixed = layout_rich("", kSize, "cost \\$5 at $x$");
            check(!mixed.runs.empty() && mixed.runs[0].text == "cost $5 at x",
                  "outside a span, \\$ is a dollar; runs on one baseline merge");
            const RichLine txt = layout_rich("", kSize, "$\\text{a b}_1$");
            check(!txt.runs.empty() && txt.runs[0].text == "a b", "\\text keeps its spaces");
            check(layout_rich("", kSize, "$\\mathrm{d}x$").runs.size() == 1, "\\mathrm groups");
            check(is_rich("$\\{x\\}$") && layout_rich("", kSize, "$\\{x\\}$").runs[0].text == "{x}",
                  "escaped braces are characters");
        }

        // --- The thread's switch -----------------------------------------------
        {
            const MathTextScope off(false);
            check(!is_rich("$x^2$") && label_width("", kSize, "$x^2$") == w("$x^2$"), "MathTextScope(false): plain");
            {
                const MathTextScope on(true);
                check(is_rich("$x^2$"), "scopes nest");
            }
            check(!is_rich("$x^2$"), "...and restore");
        }
        check(is_rich("$x^2$"), "with no scope, math is on");

        // --- Fallback fonts ------------------------------------------------------
        if (!fallback_fonts().empty()) {
            std::printf("  fallback fonts:");
            for (const FontEntry* e: fallback_fonts()) std::printf(" %s;", e->name.c_str());
            std::printf("\n");
        }

        // --- Pixels: a raised script is taller ink, and math off is literal --------
        {
            const Ink plain = ink_of(text_alone("x2"));
            const Ink rich = ink_of(text_alone("$x^2$"));
            const Ink off = ink_of(text_alone("$x^2$", false));
            check(plain.rows > 0 && rich.rows > plain.rows, "PNG: x^2's script stands above a plain 'x2'");
            check(rich.cols < plain.cols, "PNG: the smaller script is narrower");
            check(off.cols > plain.cols, "PNG: with mathtext off the dollars are drawn");
        }

        // --- SVG: tspans, plain bytes, literal when off -----------------------------
        {
            const std::string rich = svg_titled("$x^2$ [m]");
            check(rich.find("<tspan>x</tspan><tspan dy=\"-") != std::string::npos,
                  "SVG: the script is a tspan shifted up");
            check(rich.find("font-size=\"12.6\">2</tspan>") != std::string::npos, "SVG: at its own font size");
            const std::string plain = svg_titled("x2 [m]");
            check(count(plain, "<tspan") == 0, "SVG: a plain title has no tspan");
            const std::string off = svg_titled("$x^2$ [m]", false);
            check(off.find(">$x^2$ [m]</text>") != std::string::npos && count(off, "<tspan") == 0,
                  "SVG: mathtext off writes the string as given");
            Captured quiet;   // the warning set_title() sends is checked below
            const std::string bad = svg_titled("$\\alp$");
            check(bad.find(">$\\alp$</text>") != std::string::npos, "SVG: malformed math is written as given");

            // The title band grows by what the script adds.
            PlotRect fr{}, fp{};
            check(frame_of(rich, fr) && frame_of(plain, fp) && fr.y > fp.y,
                  "layout: a superscript in the title pushes the frame down");
        }

        // --- TextOptions::parse_math: one text as written, its neighbour still math -----
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            TextOptions raw;
            raw.parse_math = false;
            ax->text("$a^2$ raw", 0.2, 0.2, raw);
            ax->text("$b^2$ rich", 0.6, 0.6);
            const std::string svg = fig->render_svg().svg;
            check(svg.find(">$a^2$ raw</text>") != std::string::npos, "parse_math=false: written as given");
            check(svg.find("<tspan>b</tspan><tspan dy=") != std::string::npos, "...its neighbour is still math");
            check(svg.find(">a</tspan>") == std::string::npos, "...and no tspan of its own");
            const Ink off = ink_of(text_alone("$x^2$", false));
            FigureOptions o;
            o.width = 300;
            o.height = 160;
            o.background = Color::White;
            auto f2 = Figure::create(o);
            auto a2 = f2->add_subplot(1, 1, 1);
            AxesStyle st;
            st.show_xticks = st.show_yticks = false;
            st.spine_bottom = st.spine_left = st.spine_top = st.spine_right = false;
            a2->set_axes_style(st);
            TextOptions to;
            to.fontsize = 40.0f;
            to.color = Color::Black;
            to.ha = HAlign::Center;
            to.va = VAlign::Center;
            to.parse_math = false;
            a2->text("$x^2$", Pos::fraction(0.5), Pos::fraction(0.5), to);
            const Ink per_text = ink_of(f2->render_rgba());
            check(per_text.cols == off.cols && per_text.rows == off.rows,
                  "PNG: parse_math=false draws as the figure-wide switch does");
        }

        // --- Every site: SVG carries a tspan per rich string ---------------------------
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 2, 1);
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0}, {.name = "$\\alpha_1$"});
            ax->scatter_z(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0},
                          std::vector<double>{0.0, 1.0}, {.colorbar = true, .name = "$\\beta^2$"});
            ax->set_xtitle("$\\gamma_x$");
            ax->set_ytitle("$\\delta_y$");
            const std::vector<double> pos{0.0, 1.0};
            ax->set_xticks(pos, {"$x_0$", "$x_1$"});
            ax->set_yticks(pos, {"$y_0$", "$y_1$"});
            ax->legend();
            ax->text("$\\epsilon^1$", 0.5, 0.5);
            auto a3 = fig->add_subplot3d(1, 2, 2);
            a3->surface(PlaneOrientation::XY, std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0},
                        std::vector<double>{0.0, 1.0, 1.0, 0.0});
            a3->set_ztitle("$\\zeta_z$");
            fig->suptitle("$\\eta^2$");
            TextOptions big;
            big.fontsize = 22.0f;
            ax->text("$E = mc^2$, $\\sum_{i=0}^{n} x_i^2 \\leq \\sqrt{\\pi}\\,\\nabla\\cdot F$", 0.05, 0.85, big);
            ax->text("$e^{-x^2/2\\sigma^2}$ and $\\alpha \\in [0, 2\\pi)$, cost \\$5", 0.05, 0.65, big);
            ax->text("$\\Delta T_{\\mathrm{max}} = 3.2 \\pm 0.1\\,\\mathrm{K}$", 0.05, 0.45, big);
            ax->text("$\\sin(\\omega t) \\rightarrow \\infty$  $f'(x)$  $\\text{rate}_{\\text{in}}$", 0.05, 0.25, big);
            fig->savefig("rich_text.png");
            fig->savefig("rich_text.svg");
            const std::string svg = fig->render_svg().svg;
            // Greek letters only reach the file through the rich path.
            for (const char* g: {"\xCE\xB1", "\xCE\xB2", "\xCE\xB3", "\xCE\xB4", "\xCF\xB5", "\xCE\xB6", "\xCE\xB7"})
                check(svg.find(std::string(">") + g + "</tspan>") != std::string::npos,
                      std::string("SVG: every site goes through the seam (") + g + ")");
            check(svg.find(">x</tspan><tspan dy=") != std::string::npos
                  && svg.find(">y</tspan><tspan dy=") != std::string::npos, "SVG: tick labels too");
        }

        // --- Warnings from the call that set the string ------------------------------
        {
            Captured cap;
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_title("$\\alp$");
            check(cap.msgs.size() == 1
                  && cap.msgs[0] == "set_title: math not parsed at column 2: unknown command '\\alp'; drawn as written",
                  "warning: names the call, the column and the reason");
            ax->set_title("$\\alpha$");
            ax->set_xtitle("Price ($)");
            check(cap.msgs.size() == 1, "no warning for valid math or plain text");
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0}, {.name = "$x^$"});
            check(cap.msgs.size() == 2 && cap.msgs[1].rfind("line: math not parsed", 0) == 0, "warning: a plot's name");
            ax->text("ok $x$\n$\\bad$", 0.0, 0.0);
            check(cap.msgs.size() == 3 && cap.msgs[2].find("at line 2, column 2") != std::string::npos,
                  "warning: a multi-line text names the line");
            TextOptions raw;
            raw.parse_math = false;
            ax->text("$\\bad$", 0.0, 0.0, raw);
            check(cap.msgs.size() == 3, "no warning for a text with parse_math off");
            fig->suptitle("${$");
            check(cap.msgs.size() == 4 && cap.msgs[3].rfind("Figure::suptitle:", 0) == 0, "warning: the suptitle");

            FigureOptions o;
            o.mathtext = false;
            auto quiet = Figure::create(o);
            auto qa = quiet->add_subplot(1, 1, 1);
            qa->set_title("$\\alp$");
            qa->cla();
            qa->set_title("$\\alp$");
            check(cap.msgs.size() == 4, "no warning with mathtext off, cla() included");

            auto f3 = Figure::create();
            auto b3 = f3->add_subplot3d(1, 1, 1);
            b3->set_ztitle("$x^$");
            auto pl = b3->plane(PlaneOrientation::XY, 0.0);
            pl->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0}, {.name = "$x_$"});
            check(cap.msgs.size() == 6, "warning: 3D titles and plane objects");
        }
    }
} // namespace lt
