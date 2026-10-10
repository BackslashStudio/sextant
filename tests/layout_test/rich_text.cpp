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

        // A superscript's kern after an italic letter (kSupItalicKern).
        constexpr float kKern = 0.06f * kSize;

        float w(std::string_view s, float size = kSize) { return text_width("", size, s); }
        float wi(std::string_view s, float size = kSize) { return text_width_italic("", size, s); }

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

        // How far the ink's top quarter sits right of its bottom quarter, in px:
        // about 0 for an upright 'I', several px for a slanted one.
        float lean_of(const RgbaImage& img) {
            const Ink k = ink_of(img);
            if (k.rows == 0) return 0.0f;
            const int band = std::max(1, (k.bottom - k.top + 1) / 4);
            auto centre = [&](int y0, int y1) {
                double sx = 0.0, n = 0.0;
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < img.width; ++x) {
                        const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
                        if (img.pixels[i] < 100 && img.pixels[i + 1] < 100 && img.pixels[i + 2] < 100) {
                            sx += x;
                            n += 1.0;
                        }
                    }
                return n > 0.0 ? static_cast<float>(sx / n) : 0.0f;
            };
            return centre(k.top, k.top + band) - centre(k.bottom + 1 - band, k.bottom + 1);
        }

        // One text, alone on a bare white figure.
        RgbaImage text_alone(const std::string& s, bool mathtext = true, const std::string& font_path = "") {
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
            to.font_path = font_path;
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
                check(b.text == "x" && b.italic && b.size == kSize && b.dy == 0.0f, "x^2: the italic base on the baseline");
                check(s.text == "2" && close(s.size, kSize * 0.7f), "x^2: the script at 0.7 of the size");
                check(close(s.dy, -0.40f * kSize) && close(s.x, wi("x") + kKern), "x^2: raised 0.4 em, clear of the slanted base");
                check(!s.italic && close(l.width, wi("x") + kKern + w("2", kSize * 0.7f) + 0.05f * kSize), "x^2: width, script space included");
            }
            check(l.vm.ascent > font_vmetrics("", kSize).ascent, "x^2: reaches above the plain font");
            check(l.vm.descent == font_vmetrics("", kSize).descent, "x^2: no deeper than the plain font");

            const RichLine sub = layout_rich("", kSize, "$x_i$");
            check(sub.runs.size() == 2 && close(sub.runs[1].dy, 0.18f * kSize), "x_i: lowered 0.18 em");
            check(sub.vm.descent < font_vmetrics("", kSize).descent, "x_i: reaches below the plain font");

            const RichLine both = layout_rich("", kSize, "$x_i^2$");
            check(both.runs.size() == 3, "x_i^2: three runs");
            if (both.runs.size() == 3) {
                const RichRun& sup = both.runs[1].text == "2" ? both.runs[1] : both.runs[2];
                const RichRun& sub = both.runs[1].text == "i" ? both.runs[1] : both.runs[2];
                check(close(sup.dy, -0.40f * kSize) && close(sub.dy, 0.25f * kSize),
                      "x_i^2: the subscript drops further under a superscript");
                check(close(sub.x, wi("x")) && close(sup.x, sub.x + kKern),
                      "x_i^2: stacked, the superscript kerned clear of the slant");
            }
            const RichLine up = layout_rich("", kSize, "$2_i^2$");
            check(up.runs.size() == 3 && close(up.runs[1].x, up.runs[2].x),
                  "2_i^2: after an upright base both scripts stand at one x");

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
            check(close(bin.width, wi("a") + w("+") + wi("b") + 2.0f * (2.0f / 9.0f) * kSize, 0.01f),
                  "a+b: a medium space each side of '+'");
            // ...but not on a unary one.
            check(close(layout_rich("", kSize, "$+b$").width, w("+") + wi("b"), 0.01f), "+b: a unary '+' has none");
            const RichLine rel = layout_rich("", kSize, "$a=b$");
            check(close(rel.width, wi("a") + w("=") + wi("b") + 2.0f * (5.0f / 18.0f) * kSize, 0.01f),
                  "a=b: a thick space each side of '='");
            const float s7 = kSize * 0.7f;
            const RichLine sc = layout_rich("", kSize, "$x^{a+b}$");
            check(close(sc.width, wi("x") + kKern + wi("a", s7) + w("+", s7) + wi("b", s7) + 0.05f * kSize, 0.01f),
                  "no medium space inside a script");
            check(close(layout_rich("", kSize, "$a\\,b$").width, wi("a") + wi("b") + kSize / 6.0f, 0.01f),
                  "\\, is a thin space");
            check(close(layout_rich("", kSize, "$a\\quad b$").width, wi("a") + wi("b") + kSize, 0.01f),
                  "\\quad is an em");
            check(close(layout_rich("", kSize, "$\\sin x$").width, w("sin") + wi("x") + kSize / 6.0f, 0.01f),
                  "\\sin: upright, a thin space before its argument");

            const RichLine mixed = layout_rich("", kSize, "cost \\$5 at $x$");
            check(mixed.runs.size() == 2 && mixed.runs[0].text == "cost $5 at " && mixed.runs[1].text == "x",
                  "outside a span, \\$ is a dollar; the text run stays upright, the letter apart");
            const RichLine txt = layout_rich("", kSize, "$\\text{a b}_1$");
            check(!txt.runs.empty() && txt.runs[0].text == "a b", "\\text keeps its spaces");
            const RichLine dx = layout_rich("", kSize, "$\\mathrm{d}x$");
            check(dx.runs.size() == 2 && dx.runs[0].text == "d" && !dx.runs[0].italic && dx.runs[1].italic,
                  "\\mathrm groups, upright");
            const RichLine braces = layout_rich("", kSize, "$\\{x\\}$");
            check(is_rich("$\\{x\\}$") && braces.runs.size() == 3 && braces.runs[0].text == "{"
                  && braces.runs[1].text == "x" && braces.runs[2].text == "}",
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
            check(rich.find("<tspan font-style=\"italic\">x</tspan><tspan dx=\"") != std::string::npos,
                  "SVG: an italic x, then the script kerned");
            check(rich.find("dy=\"-") != std::string::npos, "SVG: the script is a tspan shifted up");
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
            check(svg.find("<tspan font-style=\"italic\">b</tspan><tspan dx=") != std::string::npos, "...its neighbour is still math");
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

        // --- Italic math letters (step 32b) ----------------------------------------------
        {
            std::printf("\n[rich text: italic math letters (step 32b)]\n");
            auto runs = [](std::string_view s) { return layout_rich("", kSize, s).runs; };
            auto all_italic = [&](std::string_view s, bool want) {
                const auto r = runs(s);
                return !r.empty() && std::all_of(r.begin(), r.end(), [&](const RichRun& x) { return x.italic == want; });
            };
            check(all_italic("$x$", true) && all_italic("$xyz$", true) && all_italic("$\\alpha\\omega$", true),
                  "italic: Latin letters and lower-case Greek");
            check(all_italic("$2$", false) && all_italic("$\\Gamma\\Omega$", false) && all_italic("$+$", false)
                  && all_italic("$\\infty$", false), "upright: digits, Greek capitals, symbols");
            check(all_italic("$\\sin$", false) && all_italic("$\\mathrm{xy}$", false)
                  && all_italic("$\\text{xy}$", false) && all_italic("$\\mathdefault{x}$", false),
                  "upright: function names, \\mathrm, \\text, \\mathdefault");
            check(all_italic("$\\mathit{x2\\Gamma}$", true) && runs("$\\mathit{x2\\Gamma}$").size() == 1,
                  "\\mathit: letters, digits and Greek capitals, one run");
            check(all_italic("$\\mathrm{a\\mathit{b}}$", false) == false && runs("$\\mathrm{a\\mathit{b}}$").size() == 2,
                  "fonts nest and restore");
            const auto r2 = runs("at $t$ s");
            check(r2.size() == 3 && !r2[0].italic && r2[1].italic && !r2[2].italic, "text outside a span stays upright");
            check(layout_rich("", kSize, "$x$").width == wi("x"), "an italic run measures in the italic face");

            // Discovery: each family's italic face, when it has one.
            const FontEntry* def = pick_default_font();
            if (def) {
                std::printf("  default font %s; italic: %s (face %d)\n", def->name.c_str(),
                            def->italic_path.empty() ? "none" : def->italic_path.c_str(), def->italic_index);
                check(find_font_entry("") == def && find_font_entry(def->path) == def,
                      "find_font_entry: \"\" and the file both name the default");
            }
            bool files_exist = true;
            int with_italic = 0;
            for (const FontEntry& e: discover_system_fonts())
                if (!e.italic_path.empty()) {
                    ++with_italic;
                    files_exist = files_exist && std::filesystem::exists(e.italic_path);
                }
            std::printf("  %d discovered families have an italic face\n", with_italic);
            check(files_exist, "every italic face found is a file");
            if (has_italic_face("")) {
                const char* sample = "The quick brown fox jumps over the lazy dog";
                check(wi(sample) != w(sample), "the default's italic face measures on its own");
                check(lean_of(text_alone("$I$")) > 2.0f && std::fabs(lean_of(text_alone("I"))) < 1.0f,
                      "PNG: the italic face leans, the upright one does not");
            } else {
                std::printf("  (the default font has no italic face: italic checks skipped)\n");
            }

            // No italic face (a copy discovery never listed): measured upright,
            // drawn slanted, and the SVG asks the viewer for italic.
            namespace fs = std::filesystem;
            if (def) {
                const fs::path copy = fs::temp_directory_path()
                                      / ("sextant_no_italic" + fs::path(def->path).extension().string());
                std::error_code ec;
                fs::copy_file(def->path, copy, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    const std::string cp = copy.string();
                    check(!has_italic_face(cp) && text_width_italic(cp, kSize, "xy") == text_width(cp, kSize, "xy"),
                          "no italic face: measured upright");
                    check(lean_of(text_alone("$I$", true, cp)) > 2.0f
                          && std::fabs(lean_of(text_alone("I", true, cp))) < 1.0f,
                          "PNG: no italic face, the upright one slanted");
                    auto fig = Figure::create();
                    auto ax = fig->add_subplot(1, 1, 1);
                    TextOptions to;
                    to.font_path = cp;
                    ax->text("$x$", 0.5, 0.5, to);
                    check(fig->render_svg().svg.find("<tspan font-style=\"italic\">x</tspan>") != std::string::npos,
                          "SVG: font-style=\"italic\" either way");
                    fs::remove(copy, ec);
                } else {
                    std::printf("  (could not copy the default font: synthetic-slant checks skipped)\n");
                }
            }
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
