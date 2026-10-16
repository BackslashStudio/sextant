// Math constructs past step 32 (v1.2 step 37): fractions, roots and
// over/underlines laid out by ink, the rules and paths they draw with, the SVG's
// placed form, and the room tall lines get in texts and legends. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "rich_text.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr float kSize = 20.0f;
        constexpr float kTheta = 0.0625f * kSize;   // the rule thickness at kSize
        constexpr float kSide = 0.12f * kSize;      // a fraction's side space

        bool close(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

        float wi(std::string_view s, float size = kSize) { return text_width_italic("", size, s); }
        InkBox ink(std::string_view s, float size = kSize, bool italic = true) { return ink_box("", size, s, italic); }

        // The math axis at `size`: the middle of '='.
        float axis(float size = kSize) {
            const InkBox eq = ink_box("", size, "=");
            return (eq.top + eq.bottom) * 0.5f;
        }

        const RichRun* run(const RichLine& l, std::string_view text) {
            for (const RichRun& r: l.runs)
                if (r.text == text) return &r;
            return nullptr;
        }

        float path_min_y(const RichPath& p) {
            float y = 1e9f;
            for (const auto& q: p.pts) y = std::min(y, q.y);
            return y;
        }
        float path_max_y(const RichPath& p) {
            float y = -1e9f;
            for (const auto& q: p.pts) y = std::max(y, q.y);
            return y;
        }
        bool path_has(const RichPath& p, float x, float y) {
            for (const auto& q: p.pts)
                if (close(q.x, x, 1e-2f) && close(q.y, y, 1e-2f)) return true;
            return false;
        }

        // The first number after `key` at or past `from`.
        float num_after(const std::string& s, const std::string& key, std::size_t from = 0) {
            const std::size_t at = s.find(key, from);
            return at == std::string::npos ? NAN : std::strtof(s.c_str() + at + key.size(), nullptr);
        }

        // The plot frame: the first axes-background rect in an SVG.
        bool frame_of(const std::string& svg, PlotRect& out) {
            const std::size_t at = svg.find("\" fill=\"white\"/>");
            if (at == std::string::npos) return false;
            const std::size_t start = svg.rfind("<rect x=\"", at);
            if (start == std::string::npos) return false;
            return std::sscanf(svg.c_str() + start, "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                               &out.x, &out.y, &out.w, &out.h) == 4;
        }

        // The baselines (y) of a text's lines, alone in a figure with no ticks.
        std::vector<float> text_baselines(const std::string& s) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            AxesStyle st;
            st.show_xticks = st.show_yticks = false;
            ax->set_axes_style(st);
            TextOptions to;
            to.fontsize = kSize;
            ax->text(s, 0.5, 0.5, to);
            const std::string svg = fig->render_svg().svg;
            std::vector<float> ys;
            for (std::size_t at = svg.find("<text x=\""); at != std::string::npos; at = svg.find("<text x=\"", at + 1))
                ys.push_back(num_after(svg, "y=\"", at));
            return ys;
        }

        // A one-line legend's row height for these names.
        float legend_row(std::initializer_list<const char*> names) {
            auto fs = make_snapshot(1, 1, 1);
            RenderSnapshot& s = *fs.axes[0].snap2d();
            s.legend_enabled = true;
            const auto proto = s.lines[0];
            s.lines.clear();
            for (const char* n: names) {
                s.lines.push_back(proto);
                s.lines.back().opts.name = n;
            }
            return compute_cell_decorations(s).legend_row_h;
        }
    } // namespace

    void test_math_layout() {
        std::printf("\n[math layout: fractions, roots, rules]\n");

        // --- Parsing ------------------------------------------------------------
        for (const char* s: {"$\\frac{a}{b}$", "$\\frac12$", "$\\tfrac{a}{b}$", "$\\dfrac{a}{b}$", "$\\sqrt{x}$",
                             "$\\sqrt x$", "$\\sqrt[3]{x}$", "$\\overline{AB}$", "$\\underline{x}$",
                             "$\\frac{\\sqrt{2}}{2}$"})
            check(!math_error(s), std::string("parses: ") + s);
        {
            const auto e = math_error("$\\frac{a}$");
            check(e && e->reason == "'\\frac' is missing an argument" && e->column == 2,
                  "\\frac with one argument: at its backslash");
            check(math_error("$\\sqrt[3$") && math_error("$\\sqrt[3$")->reason == "'[' is never closed",
                  "an unclosed root index");
            check(math_error("$\\sqrt[3]$") && math_error("$\\sqrt[3]$")->reason.find("missing an argument")
                                                   != std::string::npos, "an index with nothing under it");
            check(math_error("$\\overline$") != std::nullopt, "\\overline needs its argument");
            const RichLine bare = layout_rich("", kSize, "$\\sqrt$");
            check(bare.runs.size() == 1 && bare.runs[0].text == "\xE2\x88\x9A" && !bare.placed,
                  "a bare \\sqrt is the sign, as before");
            check(!layout_rich("", kSize, "$\\sqrt^2$").placed && !layout_rich("", kSize, "$x^2$").placed,
                  "nothing placed without a fraction, root or rule");
        }

        // --- Fractions ------------------------------------------------------------
        {
            const float s7 = kSize * 0.7f;
            const RichLine l = layout_rich("", kSize, "$\\frac{a}{b}$");
            const RichRun* a = run(l, "a");
            const RichRun* b = run(l, "b");
            check(l.placed && l.rules.size() == 1 && l.paths.empty() && a && b, "a/b: two runs and a bar");
            if (a && b && l.rules.size() == 1) {
                const RichRule& bar = l.rules[0];
                check(close(a->size, s7) && close(b->size, s7), "a/b: the parts one level down (0.7)");
                check(close(bar.h, std::max(kTheta, 1.0f)) && close(bar.y + bar.h * 0.5f, -axis()),
                      "a/b: a θ bar centred on the math axis");
                const float bar_top = axis() + bar.h * 0.5f, bar_bottom = axis() - bar.h * 0.5f;
                check(close(-a->dy + std::min(0.0f, ink("a", s7).bottom), bar_top + 2.0f * kTheta),
                      "a/b: 2θ between the bar and the numerator's ink");
                check(close(-b->dy + ink("b", s7).top, bar_bottom - 2.0f * kTheta),
                      "a/b: 2θ between the bar and the denominator's ink");
                const float w = std::max(wi("a", s7), wi("b", s7));
                check(close(bar.x, kSide) && close(bar.w, w) && close(l.width, w + 2.0f * kSide),
                      "a/b: the bar spans the wider part, a null delimiter space each side");
                check(l.ink_top > bar_top && l.ink_bottom < bar_bottom, "a/b: the line's ink reaches both parts");
            }
            const RichLine c = layout_rich("", kSize, "$\\frac{a}{bbb}$");
            if (const RichRun* ca = run(c, "a"); ca && !c.rules.empty())
                check(close(ca->x, kSide + (wi("bbb", s7) - wi("a", s7)) * 0.5f),
                      "a/bbb: the narrower part is centred");
            const RichLine d = layout_rich("", kSize, "$\\dfrac{a}{b}$");
            check(run(d, "a") && close(run(d, "a")->size, kSize), "\\dfrac: the parts at the line's size");
            const RichLine t = layout_rich("", kSize, "$\\tfrac{a}{b}$");
            check(run(t, "a") && close(run(t, "a")->size, s7), "\\tfrac: as \\frac");
            const RichLine n = layout_rich("", kSize, "$\\frac{\\frac{a}{b}}{c}$");
            check(n.rules.size() == 2 && run(n, "a") && close(run(n, "a")->size, kSize * 0.5f)
                  && run(n, "c") && close(run(n, "c")->size, s7), "nested: the inner parts at half size");
            const RichLine sup = layout_rich("", kSize, "$x^{\\frac{1}{2}}$");
            check(sup.rules.size() == 1 && run(sup, "1") && close(run(sup, "1")->size, kSize * 0.5f)
                  && close(sup.rules[0].h, 1.0f), "in a superscript: half size, a bar of at least a pixel");
            const RichLine on = layout_rich("", kSize, "$\\frac{a}{b}^2$");
            check(run(on, "2") && run(on, "2")->dy < -0.40f * kSize, "a script on a fraction clears it");
            const RichLine digits = layout_rich("", kSize, "$\\frac12$");
            check(digits.rules.size() == 1 && run(digits, "1") && run(digits, "2"), "\\frac12: one digit each");
            check(close(layout_rich("", kSize, "$a\\frac{1}{2}$").width,
                        wi("a") + layout_rich("", kSize, "$\\frac{1}{2}$").width),
                  "a fraction is an ordinary atom: no space beside a letter");
        }

        // --- Roots ----------------------------------------------------------------
        {
            const float bar = std::max(kTheta, 1.0f);
            const RichLine l = layout_rich("", kSize, "$\\sqrt{x}$");
            const RichRun* x = run(l, "x");
            check(l.placed && l.paths.size() == 1 && l.rules.empty() && x, "\\sqrt{x}: the radical is one path");
            if (x && l.paths.size() == 1) {
                const RichPath& p = l.paths[0];
                const float top = ink("x").top + 3.0f * kTheta + bar;
                check(path_has(p, l.width, -top) && path_has(p, l.width, -(top - bar)),
                      "\\sqrt{x}: the bar ends square over the radicand's end, 3θ over its ink");
                check(close(l.width, x->x + wi("x") + 2.0f * kTheta), "\\sqrt{x}: 2θ after the radicand");
                check(x->x > 0.4f * kSize && x->x < 0.8f * kSize, "\\sqrt{x}: the radicand right of the sign");
                check(path_max_y(p) > 0.0f, "\\sqrt{x}: the sign's foot below the baseline");
                check(path_min_y(p) >= -top - 2.0f * bar, "\\sqrt{x}: nothing far above its bar");
                check(close(l.ink_top, top) || l.ink_top > top, "\\sqrt{x}: the line's ink reaches the bar");
            }
            const RichLine tall = layout_rich("", kSize, "$\\sqrt{\\frac{a}{b}}$");
            if (tall.paths.size() == 1 && l.paths.size() == 1) {
                const bool up = path_min_y(tall.paths[0]) < path_min_y(l.paths[0]) - 0.2f * kSize
                                && tall.ink_top > l.ink_top + 0.2f * kSize;
                const bool down = tall.ink_bottom < l.ink_bottom - 0.1f * kSize;
                if (!up || !down)
                    std::printf("  radical: x top %g bottom %g path %g..%g; a/b top %g bottom %g path %g..%g\n",
                                l.ink_top, l.ink_bottom, path_min_y(l.paths[0]), path_max_y(l.paths[0]),
                                tall.ink_top, tall.ink_bottom, path_min_y(tall.paths[0]),
                                path_max_y(tall.paths[0]));
                check(up, "the radical grows up with its radicand");
                check(down, "the radical grows down with its radicand");
            } else {
                check(false, "the radical grows with its radicand");
            }
            // The foot's mitre stays near the foot (stem ≤ 0.15 em + 1 px).
            for (const RichLine* r: {&l, &tall})
                if (r->paths.size() == 1)
                    check(path_max_y(r->paths[0]) <= -r->ink_bottom + 2.0f * (0.15f * kSize + 1.0f),
                          "the radical's foot ends near its ink bottom");
            const RichLine sx = layout_rich("", kSize, "$\\sqrt x$");
            check(sx.paths.size() == 1 && sx.width == l.width, "\\sqrt x takes one atom");
            const RichLine idx = layout_rich("", kSize, "$\\sqrt[3]{x}$");
            const RichRun* three = run(idx, "3");
            const RichRun* ix = run(idx, "x");
            check(three && ix && close(three->size, kSize * 0.5f) && three->dy < -0.2f * kSize,
                  "\\sqrt[3]: the index half size, raised");
            if (three && ix) check(three->x + three->width <= ix->x, "\\sqrt[3]: the index left of the radicand");
            check(idx.width >= l.width, "\\sqrt[3]: never narrower than without");
            const RichLine many = layout_rich("", kSize, "$\\sqrt[123]{x}$");
            if (const RichRun* r = run(many, "123"); r && !many.paths.empty())
                check(r->x >= -1e-3f && many.width > idx.width, "a wide index pushes the root right");
        }

        // --- Over- and underlines ---------------------------------------------------
        {
            const float bar = std::max(kTheta, 1.0f);
            const RichLine o = layout_rich("", kSize, "$\\overline{x}$");
            check(o.rules.size() == 1 && close(o.rules[0].x, 0.0f) && close(o.rules[0].w, wi("x"))
                  && close(o.rules[0].y + o.rules[0].h, -(ink("x").top + 3.0f * kTheta)),
                  "\\overline: the body's width, 3θ over its ink");
            check(close(o.rules[0].h, bar) && close(o.width, wi("x")), "\\overline: θ thick, the body's width");
            const RichLine u = layout_rich("", kSize, "$\\underline{y}$");
            check(u.rules.size() == 1 && close(u.rules[0].y, -(ink("y").bottom - 3.0f * kTheta)),
                  "\\underline: 3θ under the body's ink");
            check(u.ink_bottom < ink("y").bottom - 3.0f * kTheta, "\\underline: the line's ink reaches its rule");
        }

        // --- SVG: the placed form -----------------------------------------------------
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});
            ax->set_title("$\\frac{a}{b} = \\sqrt{x}$");
            const std::string svg = fig->render_svg().svg;
            // The title is the only placed line: a <g> straight into its <text>.
            const std::size_t t = svg.find("\"><text x=\"");
            const std::size_t g = t == std::string::npos ? t : svg.rfind("<g ", t);
            const std::size_t end = g == std::string::npos ? g : svg.find("</g>\n", g);
            check(g != std::string::npos && end != std::string::npos, "SVG: a placed title is a <g>");
            if (g != std::string::npos && end != std::string::npos) {
                const std::string el = svg.substr(g, end - g);
                check(el.find("text-anchor=\"start\"") != std::string::npos && el.find("dx=") == std::string::npos,
                      "SVG: its runs placed absolutely, from the left end");
                check(el.find("<rect x=") != std::string::npos && el.find("<path d=\"M") != std::string::npos,
                      "SVG: the bar a <rect>, the radical a <path>");
                check(el.find("fill=\"") != std::string::npos && el.find(" Z\"/>") != std::string::npos,
                      "SVG: the <g> carries the fill the rule and path take");
                // Centred on the frame as a <text text-anchor="middle"> would be.
                PlotRect fr{};
                const float x0 = num_after(el, "<text x=\"");
                const float width = layout_rich("", AxesStyle{}.title_fontsize, "$\\frac{a}{b} = \\sqrt{x}$").width;
                check(frame_of(svg, fr) && close(x0 + width * 0.5f, fr.x + fr.w * 0.5f, 0.05f),
                      "SVG: the middle anchor resolved to the line's left end");
                const float rx = num_after(el, "<rect x=\"");
                check(close(rx, x0 + kSide * AxesStyle{}.title_fontsize / kSize, 0.01f),
                      "SVG: the bar at its place from that end");
            }
            check(t != std::string::npos && svg.find("\"><text x=\"", t + 1) == std::string::npos,
                  "SVG: only the placed line takes the <g>");
        }

        // --- Room for tall lines ---------------------------------------------------------
        {
            const std::vector<float> plain = text_baselines("a\nb\nc");
            const std::vector<float> script = text_baselines("a\n$x^2$\nc");
            const char* stacked = "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$";
            const std::vector<float> frac = text_baselines(std::string("a\n") + stacked + "\nc");
            check(plain.size() == 3 && script == plain, "a line with a script keeps the plain pitch");
            if (plain.size() == 3 && frac.size() == 3) {
                const float pitch = plain[1] - plain[0];
                const RichLine fl = layout_rich("", kSize, stacked);
                const FontVMetrics vm = font_vmetrics("", kSize);
                check(frac[1] - frac[0] > pitch + 0.5f && frac[2] - frac[1] > pitch + 0.5f,
                      "a tall fraction pushes both neighbours away");
                check(close(frac[1] - frac[0], fl.ink_top - vm.descent + 0.1f * kSize, 0.01f)
                      && close(frac[2] - frac[1], vm.ascent - fl.ink_bottom + 0.1f * kSize, 0.01f),
                      "...to 0.1 em of air between its ink and theirs");
            }
            // A small fraction moves the line above only by what it lacks (on
            // Windows' Times New Roman, nothing).
            const std::vector<float> small = text_baselines("a\n$\\frac{a}{b}$\nc");
            if (small.size() == 3 && plain.size() == 3) {
                const float pitch = plain[1] - plain[0];
                const float need = layout_rich("", kSize, "$\\frac{a}{b}$").ink_top
                                   - font_vmetrics("", kSize).descent + 0.1f * kSize;
                check(close(small[1] - small[0], std::max(pitch, need), 0.01f),
                      "a small fraction: the plain pitch, or just what its ink needs");
            }
            const float base = legend_row({"short", "$x^2$"});
            check(close(base, legend_row_height(10.0f)), "legend rows: a script name keeps the row height");
            const float tall = legend_row({"short", stacked});
            const RichLine fl = layout_rich("", 10.0f, stacked);
            check(tall > base && close(tall, fl.ink_top - fl.ink_bottom + 4.0f),
                  "legend rows: a tall name makes every row its ink plus 4 px");
        }

        // --- Warnings: the new commands no longer warn -------------------------------------
        {
            std::vector<std::string> msgs;
            Figure::set_message_handler([&](std::string_view s) { msgs.emplace_back(s); });
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_title("$\\frac{1}{2}\\sqrt[3]{x}\\overline{y}\\underline{z}$");
            check(msgs.empty(), "no warning for 37a's commands");
            ax->set_title("$\\hat{x}$");
            check(msgs.size() == 1 && msgs[0].find("not supported yet") != std::string::npos,
                  "a still deferred command still warns");
            Figure::set_message_handler(nullptr);
        }

        // --- For the eye ----------------------------------------------------------------------
        {
            FigureOptions fo;
            fo.width = 900;
            fo.height = 600;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0},
                     {.name = "$\\frac{\\alpha}{2}$"});
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{1.0, 0.0}, {.name = "$\\sqrt{x^2+y^2}$"});
            ax->legend();
            ax->set_title("$f(x) = \\frac{1}{\\sigma\\sqrt{2\\pi}} e^{-\\frac{(x-\\mu)^2}{2\\sigma^2}}$");
            ax->set_xtitle("$\\overline{v}$ [m/s], $\\underline{n}$");
            ax->set_ytitle("$\\frac{\\partial u}{\\partial t}$");
            TextOptions big;
            big.fontsize = 24.0f;
            ax->text("$\\frac{a+b}{c} \\quad \\dfrac{1}{2} \\quad \\frac{\\frac{1}{x}+1}{y}$", 0.05, 0.85, big);
            ax->text("$\\sqrt{2}\\quad\\sqrt[3]{x}\\quad\\sqrt{\\frac{a}{b}}\\quad\\sqrt{x_i^2+y}$", 0.05, 0.62, big);
            ax->text("$\\overline{AB}\\quad\\underline{xyz}\\quad x^{\\frac{1}{2}}$\nnext line\n$\\frac{1}{2}$ below",
                     0.05, 0.18, big);
            TextOptions turned = big;
            turned.rotation = 30.0f;
            ax->text("$\\frac{1}{\\sqrt{n}}$", 0.8, 0.45, turned);
            fig->savefig("math_layout.png");
            fig->savefig("math_layout.svg");
        }
    }
} // namespace lt
