// Math constructs past step 32 (v1.2 step 37): fractions, roots and
// over/underlines laid out by ink, the rules and paths they draw with, the SVG's
// placed form, and the room tall lines get in texts and legends. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "rich_text.h"

#include <algorithm>
#include <filesystem>

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

        // --- Accents (37b) ---------------------------------------------------------------
        {
            std::printf("\n[math layout: accents]\n");
            const float lift = 2.0f * kTheta;   // the accent's ink over the body's
            for (const char* s: {"$\\hat{x}$", "$\\bar{x}$", "$\\tilde{x}$", "$\\dot{x}$", "$\\ddot{x}$",
                                 "$\\acute{x}$", "$\\grave{x}$", "$\\breve{x}$", "$\\check{x}$", "$\\mathring{x}$",
                                 "$\\vec{x}$", "$\\widehat{xyz}$", "$\\widetilde{xyz}$", "$\\overrightarrow{AB}$",
                                 "$\\overleftarrow{AB}$", "$\\hat x$", "$\\hat{\\imath}$"}) {
                check(!math_error(s), std::string("parses: ") + s);
                check(layout_rich("", kSize, s).placed, std::string("placed: ") + s);
            }
            check(math_error("$\\hat$") && math_error("$\\hat$")->reason == "'\\hat' is missing an argument",
                  "an accent needs its argument");

            // Over an italic letter: centred on its ink, moved right by half the
            // slant of its height, 2θ clear of it; the letter's width kept.
            const RichLine hx = layout_rich("", kSize, "$\\hat{x}$");
            const RichRun* x = run(hx, "x");
            const RichRun* hat = run(hx, "\xCB\x86");
            check(hx.runs.size() == 2 && x && hat && close(hat->size, kSize), "\\hat{x}: the letter and ˆ");
            if (x && hat) {
                const InkBox ix = ink("x"), ia = ink("\xCB\x86", kSize, false);
                const float skew = 0.2f * ix.top * 0.5f;
                check(close(hat->x + (ia.left + ia.right) * 0.5f, x->x + (ix.left + ix.right) * 0.5f + skew, 0.01f),
                      "\\hat{x}: centred on the letter's ink, half the slant to the right");
                check(close(-hat->dy + ia.bottom, ix.top + lift, 0.01f), "\\hat{x}: 2θ over the letter's ink");
                check(close(hx.width, wi("x")), "\\hat{x}: the letter's width");
                check(close(hx.ink_top, -hat->dy + ia.top, 0.01f) && hx.vm.ascent >= hx.ink_top,
                      "\\hat{x}: the line reaches the accent's ink");
            }
            const RichLine h2 = layout_rich("", kSize, "$\\hat{2}$");
            if (const RichRun* two = run(h2, "2"), *acc = run(h2, "\xCB\x86"); two && acc) {
                const InkBox i2 = ink("2", kSize, false), ia = ink("\xCB\x86", kSize, false);
                check(close(acc->x + (ia.left + ia.right) * 0.5f, (i2.left + i2.right) * 0.5f, 0.01f),
                      "\\hat{2}: over an upright digit, no skew");
            }
            const RichLine hX = layout_rich("", kSize, "$\\hat{X}$");
            if (const RichRun* a = run(hX, "\xCB\x86"); a && hat)
                check(a->dy < hat->dy - 0.1f * kSize, "\\hat{X} sits higher than \\hat{x}");
            const RichLine group = layout_rich("", kSize, "$\\bar{xy}$");
            if (const RichRun* a = run(group, "\xC2\xAF"); a && !group.runs.empty()) {
                const InkBox ig = ink("xy"), ia = ink("\xC2\xAF", kSize, false);
                check(close(a->x + (ia.left + ia.right) * 0.5f, (ig.left + ig.right) * 0.5f, 0.01f),
                      "\\bar{xy}: centred on the group's ink, no skew");
            }
            const RichLine vec = layout_rich("", kSize, "$\\vec{v}$");
            check(run(vec, "\xE2\x86\x92") && close(run(vec, "\xE2\x86\x92")->size, 0.6f * kSize),
                  "\\vec: a small arrow");
            const RichLine sup = layout_rich("", kSize, "$\\hat{x}^2$");
            const RichLine plain = layout_rich("", kSize, "$x^2$");
            // The script goes up only by what the accent adds above the font's
            // ascent (32a's rule for a tall nucleus): nothing on Windows' Times New
            // Roman, a little on macOS's Times.
            const float above = std::max(0.0f, hx.ink_top - font_vmetrics("", kSize).ascent);
            if (run(sup, "2") && run(plain, "2"))
                check(close(run(sup, "2")->dy, run(plain, "2")->dy - above, 0.01f),
                      "\\hat{x}^2: the script where x^2's is, raised by what the accent adds");

            // Wide accents: paths across the body's ink, 2θ clear of it.
            const RichLine wh = layout_rich("", kSize, "$\\widehat{xyz}$");
            const InkBox ib = ink("xyz");
            check(wh.paths.size() == 1 && wh.runs.size() == 1, "\\widehat: one path over the body");
            if (wh.paths.size() == 1) {
                float lo = 1e9f, hi = -1e9f;
                for (const auto& q: wh.paths[0].pts) {
                    lo = std::min(lo, q.x);
                    hi = std::max(hi, q.x);
                }
                const float skewless = (ib.left + ib.right) * 0.5f;
                check(close((lo + hi) * 0.5f, skewless, 0.5f) && hi - lo >= ib.right - ib.left - 0.01f,
                      "\\widehat: spans the body's ink, centred");
                check(close(-path_max_y(wh.paths[0]), ib.top + lift, 0.05f), "\\widehat: 2θ over the ink");
                check(-path_min_y(wh.paths[0]) - ib.top < 0.45f * kSize, "\\widehat: not too tall");
            }
            const RichLine wt = layout_rich("", kSize, "$\\widetilde{xyz}$");
            check(wt.paths.size() == 1 && -path_min_y(wt.paths[0]) - ib.top < 0.35f * kSize,
                  "\\widetilde: one low wave over the body");
            const RichLine ra = layout_rich("", kSize, "$\\overrightarrow{AB}$");
            const RichLine la = layout_rich("", kSize, "$\\overleftarrow{AB}$");
            check(ra.paths.size() == 2 && la.paths.size() == 2, "the over-arrows: a shaft and a head");
            if (ra.paths.size() == 2 && la.paths.size() == 2) {
                const auto right_of = [](const RichPath& p) {
                    float hi = -1e9f;
                    for (const auto& q: p.pts) hi = std::max(hi, q.x);
                    return hi;
                };
                const auto left_of = [](const RichPath& p) {
                    float lo = 1e9f;
                    for (const auto& q: p.pts) lo = std::min(lo, q.x);
                    return lo;
                };
                // The head is the second path: at the right for →, the left for ←.
                check(right_of(ra.paths[1]) > right_of(ra.paths[0]) - 1.0f && left_of(ra.paths[1]) > left_of(ra.paths[0]),
                      "\\overrightarrow: the head at the right end");
                check(left_of(la.paths[1]) < left_of(la.paths[0]) + 1.0f && right_of(la.paths[1]) < right_of(la.paths[0]),
                      "\\overleftarrow: the head at the left end");
            }
            const RichLine narrow = layout_rich("", kSize, "$\\widehat{i}$");
            if (narrow.paths.size() == 1) {
                float lo = 1e9f, hi = -1e9f;
                for (const auto& q: narrow.paths[0].pts) {
                    lo = std::min(lo, q.x);
                    hi = std::max(hi, q.x);
                }
                check(hi - lo >= 0.5f * kSize - 0.01f, "a wide accent is half an em at least");
            }

            // SVG: an accent's line is placed (its run steps back over the letter).
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_title("$\\hat{x}$");
            const std::string svg = fig->render_svg().svg;
            const std::size_t t = svg.find("\"><text x=\"");
            check(t != std::string::npos && svg.find("dx=") == std::string::npos
                  && svg.find("<tspan x=\"", t) != std::string::npos, "SVG: an accented title is placed");
        }

        // --- Delimiters (37c) ------------------------------------------------------------
        {
            std::printf("\n[math layout: delimiters]\n");
            const float ax = axis();
            const float s7 = kSize * 0.7f;
            for (const char* s: {"$\\left(x\\right)$", "$\\left[\\frac{a}{b}\\right]$", "$\\left\\{x\\middle|y\\right\\}$",
                                 "$\\left.\\frac{a}{b}\\right|$", "$\\left<x\\right>$", "$\\left\\langle x\\right\\rangle$",
                                 "$\\left\\lfloor x\\right\\rfloor$", "$\\left\\lceil x\\right\\rceil$",
                                 "$\\left/x\\right\\backslash$", "$\\left\\|x\\right\\|$", "$\\left\\Vert x\\right\\Vert$",
                                 "$\\left\\vert x\\right\\vert$", "$\\left\\lbrace x\\right\\rbrace$", "$\\bigl(x\\bigr)$",
                                 "$\\Big[$", "$\\bigg\\{$", "$\\Bigg|$", "$\\bigm|$", "$\\binom{n}{k}$",
                                 "$\\left(x\\right)^2$", "$\\left( \\left[ x \\right] \\right)$"})
                check(!math_error(s), std::string("parses: ") + s);
            const auto reason = [](std::string_view s) {
                const auto e = math_error(s);
                return e ? e->reason : std::string();
            };
            check(reason("$\\left(x$") == "'\\left' is never closed", "an unclosed \\left");
            check(reason("$x\\right)$") == "'\\right' without a '\\left'", "a \\right with no \\left");
            check(reason("$x\\middle|y$") == "'\\middle' without a '\\left'", "a \\middle with no \\left");
            check(reason("$\\left x\\right)$") == "'\\left' needs a delimiter", "\\left needs a delimiter");
            check(reason("$\\left(x\\right$") == "'\\right' needs a delimiter", "\\right needs a delimiter");
            check(reason("$\\big$") == "'\\big' needs a delimiter", "\\big needs a delimiter");
            check(reason("${\\left(x}\\right)$") == "'}' without a '{'", "a group cannot close inside \\left");

            // Small contents: the font's own glyphs, centred on the axis, as (x).
            const RichLine px = layout_rich("", kSize, "$\\left(x\\right)$");
            const RichRun* open = run(px, "(");
            check(open && run(px, ")") && px.paths.empty() && px.rules.empty() && !px.placed,
                  "\\left(x\\right): the font's parentheses");
            if (open) {
                const InkBox ip = ink("(", kSize, false);
                check(close(-open->dy + (ip.top + ip.bottom) * 0.5f, ax, 0.01f), "...centred on the math axis");
            }
            check(close(px.width, layout_rich("", kSize, "$(x)$").width), "...as wide as (x)");

            // Tall contents: drawn, TeX's height, symmetric about the axis.
            const RichLine fl = layout_rich("", kSize, "$\\frac{a}{b}$");
            const float h = 2.0f * std::max(fl.ink_top - ax, ax - fl.ink_bottom);
            const float target = std::max(0.901f * h, h - 0.5f * kSize);
            const RichLine pf = layout_rich("", kSize, "$\\left(\\frac{a}{b}\\right)$");
            check(pf.placed && pf.paths.size() == 2 && pf.rules.size() == 1, "(a/b): two drawn parentheses");
            const float tol = 0.15f * kSize;
            if (pf.paths.size() == 2) {
                for (const RichPath& p: pf.paths) {
                    check(close(-path_min_y(p), ax + target * 0.5f, tol) && close(-path_max_y(p), ax - target * 0.5f, tol),
                          "(a/b): each parenthesis TeX's height, centred on the axis");
                }
                float l0 = 1e9f, r1 = -1e9f;
                for (const auto& q: pf.paths[0].pts) l0 = std::min(l0, q.x);
                for (const auto& q: pf.paths[1].pts) r1 = std::max(r1, q.x);
                check(l0 >= 0.0f && r1 <= pf.width + 1e-3f && pf.width > fl.width, "(a/b): both inside the box, around the fraction");
            }
            const RichLine both = layout_rich("", kSize, "$\\left(\\frac{a}{b}\\middle|\\frac{c}{d}\\right)$");
            check(both.paths.size() == 2 && both.rules.size() == 3, "\\middle: a drawn bar between two fractions");
            const RichLine none = layout_rich("", kSize, "$\\left.\\frac{a}{b}\\right|$");
            check(none.paths.empty() && none.rules.size() == 2 && run(none, "a")
                  && close(run(none, "a")->x, 0.12f * kSize + 0.12f * kSize + (std::max(wi("a", s7), wi("b", s7)) - wi("a", s7)) * 0.5f, 0.01f),
                  "\\left.: nothing drawn, a null delimiter's space");
            check(layout_rich("", kSize, "$\\left[\\frac{a}{b}\\right]$").rules.size() == 7, "[a/b]: brackets of three rules");
            check(layout_rich("", kSize, "$\\left\\lfloor\\frac{a}{b}\\right\\rfloor$").rules.size() == 5
                  && layout_rich("", kSize, "$\\left\\lceil\\frac{a}{b}\\right\\rceil$").rules.size() == 5,
                  "floor and ceiling: two rules each");
            check(layout_rich("", kSize, "$\\left\\|\\frac{a}{b}\\right\\|$").rules.size() == 5, "\\|: two rules each");
            for (const char* s: {"$\\left<\\frac{a}{b}\\right>$", "$\\left/\\frac{a}{b}\\right\\backslash$",
                                 "$\\left\\{\\frac{a}{b}\\right\\}$"})
                check(layout_rich("", kSize, s).paths.size() == 2, std::string("drawn: ") + s);
            const RichLine br = layout_rich("", kSize, "$\\left\\{\\frac{a}{b}\\right\\}$");
            if (br.paths.size() == 2) {
                float lx = 1e9f, ly = 0.0f;
                for (const auto& q: br.paths[0].pts)
                    if (q.x < lx) {
                        lx = q.x;
                        ly = q.y;
                    }
                check(close(-ly, ax, 0.05f * kSize), "\\{: its cusp on the axis");
            }
            // Nested: the outer pair is sized by TeX's rule to the inner one's ink
            // (so it may come out a little shorter, as in TeX).
            const RichLine nest = layout_rich("", kSize, "$\\left(\\left[\\frac{a}{b}\\right]\\right)$");
            if (nest.paths.size() == 2 && !nest.rules.empty()) {
                const RichRule& stem_rule = nest.rules[0];   // the inner '[' stem
                const float hi = 2.0f * std::max(-stem_rule.y - ax, ax + stem_rule.y + stem_rule.h);
                const float want = std::max(0.901f * hi, hi - 0.5f * kSize);
                check(close(path_max_y(nest.paths[0]) - path_min_y(nest.paths[0]), want, tol),
                      "nested: the outer pair sized to the inner by TeX's rule");
            } else {
                check(false, "nested: the outer pair sized to the inner by TeX's rule");
            }

            // \big and its kin: amsmath's 1.2, 1.8, 2.4, 3.0 em, centred on the axis.
            float prev = 0.0f;
            for (const auto& [cmd, em]: {std::pair{"big", 1.2f}, {"Big", 1.8f}, {"bigg", 2.4f}, {"Bigg", 3.0f}}) {
                const RichLine b = layout_rich("", kSize, std::string("$\\") + cmd + "($");
                const bool ok = b.paths.size() == 1;
                const float ht = ok ? path_max_y(b.paths[0]) - path_min_y(b.paths[0]) : 0.0f;
                check(ok && close(ht, em * kSize, tol) && ht > prev
                      && close(-(path_max_y(b.paths[0]) + path_min_y(b.paths[0])) * 0.5f, ax, 0.05f * kSize),
                      std::string("\\") + cmd + "(: " + std::to_string(em).substr(0, 3) + " em, on the axis");
                prev = ht;
            }
            check(close(layout_rich("", kSize, "$a\\bigm|b$").width - layout_rich("", kSize, "$a\\big|b$").width,
                        2.0f * (5.0f / 18.0f) * kSize, 0.01f),
                  "\\bigm is a relation, \\big ordinary");
            check(close(layout_rich("", kSize, "$a\\bigl(b$").width, layout_rich("", kSize, "$a\\big(b$").width),
                        "\\bigl opens: no space after an ordinary atom");

            // \binom: the parts one level down, no bar, in parentheses sized to them.
            const RichLine bn = layout_rich("", kSize, "$\\binom{n}{k}$");
            const RichRun* n = run(bn, "n");
            const RichRun* k = run(bn, "k");
            check(n && k && close(n->size, s7) && close(n->x, k->x, 0.5f * kSize) && n->dy < k->dy,
                  "\\binom: n over k at script size");
            check(bn.rules.empty() && (bn.paths.size() == 2 || (run(bn, "(") && run(bn, ")"))),
                  "\\binom: no bar, two parentheses");
            if (n) check(close(-n->dy + std::min(0.0f, ink("n", s7).bottom), ax + 2.0f * kTheta, 0.01f),
                         "\\binom: the numerator 2θ over the axis");
            const RichLine sc = layout_rich("", kSize, "$\\left(x\\right)^2$");
            check(run(sc, "2") && run(sc, "2")->dy < 0.0f, "a script on \\right)");
        }

        // --- Big operators and limits ------------------------------------------------------
        {
            const float s7 = kSize * 0.7f, big = 1.4f * kSize, gap = 0.11f * kSize;
            const float ax = axis();
            const std::string sum = "\xE2\x88\x91", integral = "\xE2\x88\xAB";
            for (const char* s: {"$\\sum_{i=0}^{n}$", "$\\bigoplus_k$", "$\\lim_{x\\to 0}$", "$\\overset{!}{=}$",
                                 "$\\underset{x}{y}$", "$a\\stackrel{f}{\\to}b$", "$\\operatorname{argmax}_x$",
                                 "$\\sum_{\\substack{0<i<n\\\\j\\neq i}}$", "$\\substack{a\\\\b\\\\c}$",
                                 "$\\bigvee\\bigwedge\\bigotimes\\bigodot\\biguplus\\bigsqcup$"})
                check(!math_error(s), std::string("parses: ") + s);
            const auto reason = [](const char* s) {
                const auto e = math_error(s);
                return e ? e->reason : std::string();
            };
            check(reason("$\\substack{a\\\\b$") == "'{' is never closed", "an unclosed \\substack");
            check(reason("$a\\\\b$") == "'\\\\' is not supported", "\\\\ outside \\substack stays malformed");
            check(reason("$\\overset{a}$") == "'\\overset' is missing an argument", "\\overset needs two arguments");

            // 1.4x at level 0, the ink centred on the axis; in a script, as any glyph.
            const RichLine bare = layout_rich("", kSize, "$\\sum$");
            const RichRun* op = run(bare, sum);
            const InkBox si = ink(sum, big, false);
            check(op && close(op->size, big) && bare.placed, "\\sum: 1.4x at level 0");
            if (op) check(close(-op->dy + (si.top + si.bottom) * 0.5f, ax, 0.01f), "\\sum: its ink centred on the axis");
            const RichLine sc = layout_rich("", kSize, "$x^{\\sum}$");
            check(run(sc, sum) && close(run(sc, sum)->size, s7), "\\sum in a script: the script size, unenlarged");
            const RichLine fr = layout_rich("", kSize, "$\\frac{\\sum}{2}$");
            check(run(fr, sum) && close(run(fr, sum)->size, s7), "\\sum in a fraction: one level down, unenlarged");

            // Limits: one level down, centred, 0.11 em clear of the operator's ink.
            const RichLine lim = layout_rich("", kSize, "$\\sum_{i=0}^{n}$");
            const RichRun* o = run(lim, sum);
            const RichRun* n = run(lim, "n");
            const RichRun* z = run(lim, "=0");   // "=" and "0" merge
            check(o && n && z && close(n->size, s7) && close(z->size, s7), "\\sum_{i=0}^{n}: limits at script size");
            if (o && n && z) {
                const float op_top = -o->dy + si.top, op_bottom = -o->dy + si.bottom;
                check(close(-n->dy + std::min(0.0f, ink("n", s7).bottom), op_top + gap, 0.01f),
                      "...the upper limit's ink 0.11 em over the operator's");
                const float under_top = std::max({ink("i", s7).top, ink("=", s7, false).top, ink("0", s7, false).top});
                check(close(-z->dy + under_top, op_bottom - gap, 0.01f), "...the lower limit's ink 0.11 em under it");
                check(close(n->x + n->width * 0.5f, o->x + o->width * 0.5f, 0.01f), "...the upper limit centred");
                check(lim.placed && close(lim.width, std::max(o->width, z->x + z->width - run(lim, "i")->x), 0.01f),
                      "...the widest of the three sets the width");
            }
            const RichLine wide = layout_rich("", kSize, "$\\sum_{i=0,\\ldots,1000}$");
            const RichRun* wo = run(wide, sum);
            check(wo && wo->x > 0.0f && wide.width > wo->width + 0.5f * kSize
                  && close(wo->x + wo->width * 0.5f, wide.width * 0.5f, 0.01f),
                  "a limit wider than the operator widens the box, the operator centred");
            const RichLine sp = layout_rich("", kSize, "$\\sum_{i}^{n}$");
            check(run(sp, "i") && run(sp, "n") && run(sp, "i")->dy > 0.0f
                  && run(sp, "n")->dy < -0.5f * kSize, "limits above and below, not at the side");

            // \lim and friends: limits under the upright name, at every level.
            const RichLine l0 = layout_rich("", kSize, "$\\lim_{x\\to 0}$");
            const RichRun* lr = run(l0, "lim");
            const RichRun* lx = run(l0, "x");
            check(lr && lx && close(lr->size, kSize) && !lr->italic && lx->dy > 0.0f && close(lx->size, s7),
                  "\\lim_{x\\to 0}: the limit under lim");
            const RichLine mx = layout_rich("", kSize, "$a^{\\max_k b}$");
            const RichRun* k = run(mx, "k");
            const RichRun* mr = run(mx, "max");
            check(k && mr && close(k->size, 0.5f * kSize) && k->dy > mr->dy, "\\max in a script: its limit under it too");
            const RichLine sn = layout_rich("", kSize, "$\\sin_x$");
            check(run(sn, "x") && run(sn, "sin") && close(run(sn, "x")->x, run(sn, "sin")->width, 0.01f),
                  "\\sin keeps side scripts");

            // Integrals: side scripts by the glyph's ink, superscript high, subscript low.
            const RichLine in = layout_rich("", kSize, "$\\int_0^1$");
            const RichRun* ig = run(in, integral);
            const RichRun* one = run(in, "1");
            const RichRun* zero = run(in, "0");
            check(ig && one && zero && close(ig->size, big), "\\int_0^1: a 1.4x integral, two scripts");
            if (ig && one && zero) {
                const InkBox ii = ink(integral, big, false);
                const float top = -ig->dy + ii.top, bottom = -ig->dy + ii.bottom;
                check(close(-one->dy, top - 0.386f * s7, 0.01f), "...the superscript's baseline from the ink top");
                check(close(-zero->dy, bottom - 0.05f * s7, 0.01f), "...the subscript's baseline from the ink bottom");
                check(one->x >= ig->x + ig->width - 1e-3f && zero->x <= one->x + 1e-3f && zero->x > ig->x,
                      "...the superscript past the advance, the subscript no further right");
                // The glyph's own reach beside the subscript (glyph coordinates: + dy).
                const InkBox low = ink_band("", big, integral, ii.bottom,
                                            bottom - 0.05f * s7 + ink("0", s7, false).top + ig->dy);
                check(low.right > low.left && close(zero->x, ig->x + low.right + 0.05f * kSize, 0.01f),
                      "...the subscript 0.05 em clear of the glyph beside it");
                const RichLine again = layout_rich("", kSize, "$\\int_0^1$");
                check(run(again, "0") && run(again, "0")->x == zero->x, "...the same on a second layout");
            }

            // \overset, \underset (ordinary), \stackrel (a relation); \operatorname.
            const RichLine ov = layout_rich("", kSize, "$\\overset{!}{=}$");
            check(run(ov, "!") && run(ov, "=") && close(run(ov, "!")->size, s7) && run(ov, "!")->dy < 0.0f,
                  "\\overset{!}{=}: ! over =, one level down");
            const RichLine un = layout_rich("", kSize, "$\\underset{x}{y}$");
            check(run(un, "x") && run(un, "y") && run(un, "x")->dy > 0.0f, "\\underset: under");
            check(close(layout_rich("", kSize, "$a\\stackrel{f}{=}b$").width
                            - layout_rich("", kSize, "$a\\overset{f}{=}b$").width,
                        2.0f * (5.0f / 18.0f) * kSize, 0.01f),
                  "\\stackrel is a relation, \\overset ordinary");
            float argmax = 0.0f;   // one atom a letter, as \mathrm
            for (const char* c: {"a", "r", "g", "m", "a", "x"}) argmax += text_width("", kSize, c);
            check(close(layout_rich("", kSize, "$\\operatorname{argmax} x$").width, argmax + wi("x") + kSize / 6.0f, 0.01f),
                  "\\operatorname: upright, spaced as \\sin");

            // \substack: rows centred, their ink 0.1 em apart.
            const RichLine ss = layout_rich("", kSize, "$\\sum_{\\substack{a\\\\b}}$");
            const RichRun* ra = run(ss, "a");
            const RichRun* rb = run(ss, "b");
            check(ra && rb && rb->dy > ra->dy && close(ra->size, s7) && close(rb->size, s7),
                  "\\substack: rows one under another, at the limit's size");
            if (ra && rb)
                check(close(-ra->dy + std::min(0.0f, ink("a", s7).bottom) - (-rb->dy + ink("b", s7).top),
                            0.1f * s7, 0.01f), "...their ink 0.1 em apart");
        }

        // --- Bold and the math alphabets ---------------------------------------------------
        {
            const float s7 = kSize * 0.7f;
            for (const char* s: {"$\\mathbf{x}$", "$\\mathcal{A}$", "$\\mathscr{L}$", "$\\mathbb{R}$", "$\\mathfrak{g}$",
                                 "$\\mathsf{x1}$", "$\\mathtt{x1}$", "$\\mathbf{F}^{\\mathbf{2}}$"})
                check(!math_error(s), std::string("parses: ") + s);
            check(math_error("$\\mathbf x$") && math_error("$\\mathbf x$")->reason == "'\\mathbf' needs '{'",
                  "\\mathbf needs a group");

            // \mathbf: every glyph bold and upright, measured in the bold face.
            const float bw = text_width_face("", kSize, "x", FontFace::Bold) + text_width_face("", kSize, "2", FontFace::Bold);
            const RichLine bf = layout_rich("", kSize, "$\\mathbf{x2}$");
            check(bf.runs.size() == 1 && bf.runs[0].text == "x2" && bf.runs[0].bold && !bf.runs[0].italic
                  && close(bf.width, bw), "\\mathbf{x2}: one bold upright run, the bold face's width");
            const RichLine bs = layout_rich("", kSize, "$x^{\\mathbf{n}}$");
            check(run(bs, "n") && run(bs, "n")->bold && close(run(bs, "n")->size, s7), "\\mathbf in a script");
            check(!run(layout_rich("", kSize, "$\\mathbf{x}y$"), "y")->bold, "bold ends with its group");
            const FontEntry* def = pick_default_font();
            if (has_face("", FontFace::Bold) && def && def->bold_index == 0) {
                check(text_width_face("", kSize, "Wx", FontFace::Bold) == text_width(def->bold_path, kSize, "Wx"),
                      "the bold face is the family's own (" + def->bold_path + ")");
            } else {
                std::printf("  (the default font has no bold face, or it is a collection's later face: bold-face check skipped)\n");
            }
            // No bold face (a copy discovery never listed): upright plus 0.04 em,
            // drawn twice, and the SVG asks for bold.
            namespace fs = std::filesystem;
            if (def) {
                const fs::path copy = fs::temp_directory_path() / ("sextant_no_bold" + fs::path(def->path).extension().string());
                std::error_code ec;
                fs::copy_file(def->path, copy, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    const std::string cp = copy.string();
                    check(!has_face(cp, FontFace::Bold)
                          && close(text_width_face(cp, kSize, "xy", FontFace::Bold), text_width(cp, kSize, "xy") + 0.04f * kSize),
                          "no bold face: the upright width plus 0.04 em");
                    const InkBox up = ink_box(cp, kSize, "I", FontFace::Upright), fb = ink_box(cp, kSize, "I", FontFace::Bold);
                    check(close(fb.right, up.right + 0.04f * kSize) && close(fb.left, up.left), "...its ink 0.04 em wider");
                    const auto ink_px = [&](const char* s) {
                        FigureOptions fo;
                        fo.width = 200;
                        fo.height = 120;
                        auto fig = Figure::create(fo);
                        auto ax = fig->add_subplot(1, 1, 1);
                        AxesStyle st;
                        st.show_xticks = st.show_yticks = false;
                        st.spine_bottom = st.spine_left = st.spine_top = st.spine_right = false;
                        ax->set_axes_style(st);
                        TextOptions to;
                        to.fontsize = 40.0f;
                        to.font_path = cp;
                        ax->text(s, Pos::fraction(0.3), Pos::fraction(0.3), to);
                        const RgbaImage img = fig->render_rgba();
                        int n = 0;
                        for (std::size_t k = 0; k + 3 < img.pixels.size(); k += 4) n += img.pixels[k] < 128;
                        return n;
                    };
                    check(ink_px("$\\mathbf{I}$") > ink_px("$\\mathrm{I}$") + 20, "PNG: a faux bold I has more ink than an upright one");
                    auto fig = Figure::create();
                    auto ax = fig->add_subplot(1, 1, 1);
                    TextOptions to;
                    to.font_path = cp;
                    ax->text("$\\mathbf{v}$", 0.5, 0.5, to);
                    check(fig->render_svg().svg.find("font-weight=\"bold\">v</tspan>") != std::string::npos,
                          "SVG: a bold run is font-weight bold");
                    fs::remove(copy, ec);
                }
            }

            // The alphabets: Unicode's math alphanumerics, the Letterlike holes, digits where they exist.
            const auto cps = [](const RichLine& l) {
                std::u32string out;
                for (const RichRun& r: l.runs)
                    for (std::size_t i = 0; i < r.text.size();) {
                        const auto b0 = static_cast<unsigned char>(r.text[i]);
                        const std::size_t n = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
                        char32_t cp = n == 1 ? b0 : n == 2 ? (b0 & 0x1Fu) : n == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
                        for (std::size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(r.text[i + k]) & 0x3Fu);
                        out += cp;
                        i += n;
                    }
                return out;
            };
            const struct {
                const char* src;
                std::u32string want;
            } alphas[] = {
                {"$\\mathcal{ABEaeo1}$", {0x1D49C, 0x212C, 0x2130, 0x1D4B6, 0x212F, 0x2134, '1'}},
                {"$\\mathscr{LMz}$", {0x2112, 0x2133, 0x1D4CF}},
                {"$\\mathbb{ARZa09}$", {0x1D538, 0x211D, 0x2124, 0x1D552, 0x1D7D8, 0x1D7E1}},
                {"$\\mathfrak{ACRg2}$", {0x1D504, 0x212D, 0x211C, 0x1D524, '2'}},
                {"$\\mathsf{Aa0}$", {0x1D5A0, 0x1D5BA, 0x1D7E2}},
                {"$\\mathtt{Zz9}$", {0x1D689, 0x1D6A3, 0x1D7FF}},
            };
            for (const auto& a: alphas) {
                const RichLine l = layout_rich("", kSize, a.src);
                check(cps(l) == a.want && std::none_of(l.runs.begin(), l.runs.end(), [](const RichRun& r) { return r.italic || r.bold; }),
                      std::string(a.src) + ": its code points, upright");
            }
            check(cps(layout_rich("", kSize, "$\\mathbb{R}^n$")) == std::u32string{0x211D, 'n'}
                  && run(layout_rich("", kSize, "$\\mathbb{R}^n$"), "n")->italic, "the alphabet ends with its group");
            check(cps(layout_rich("", kSize, "$\\mathcal{\\alpha+}$")) == std::u32string{0x03B1, '+'},
                  "other characters as \\mathrm");

            // The fallback chain: Cambria Math last on Windows, so every glyph found
            // before keeps its font; the alphabets' glyphs found somewhere.
            std::printf("  fallback chain:");
            for (const FontEntry* e: fallback_fonts()) std::printf(" %s#%d;", e->name.c_str(), e->index);
            std::printf("\n");
#if defined(_WIN32)
            check(!fallback_fonts().empty() && fallback_fonts().back()->name == "Cambria Math"
                  && fallback_fonts().back()->index == 1, "Windows: Cambria Math, cambria.ttc's face 1, last");
#endif
            int found = 0;
            for (const char* g: {"\xF0\x9D\x92\x9C", "\xE2\x84\x9D", "\xF0\x9D\x94\x84", "\xF0\x9D\x96\xA0", "\xF0\x9D\x99\xB0"}) {
                const InkBox ib = ink_box("", kSize, g);
                found += ib.right > ib.left;
            }
            std::printf("  math alphabet samples with a glyph: %d of 5\n", found);
#if defined(_WIN32) || defined(__APPLE__)
            check(found == 5, "every alphabet has its glyphs in the chain");
#endif
        }

        // --- Font sets ---------------------------------------------------------------------
        {
            std::string msg;
            check(resolve_math_font(MathFontset::Text, "", msg).empty() && msg.empty(), "Text: the text font, no message");
            for (const auto& [set, first, names] :
                 {std::tuple{MathFontset::Stix, "STIX Two Math", "STIX Two Math, STIX Two Text, STIXGeneral"},
                  std::tuple{MathFontset::Cm, "Latin Modern Math", "Latin Modern Math, CMU Serif, Latin Modern Roman"}}) {
                const std::string got = resolve_math_font(set, "", msg);
                const char* label = set == MathFontset::Stix ? "Stix" : "Cm";
                const FontEntry* fam = find_family(first);
                if (!got.empty()) {
                    std::printf("  %s resolves to %s\n", label, got.c_str());
                    check(msg.empty() && find_font_entry(got) != nullptr, std::string(label) + ": an installed family, no message");
                    if (fam) check(got == fam->path, std::string(label) + ": the first of its list");
                } else {
                    std::printf("  %s: none installed\n", label);
                    check(msg == std::string("math_fontset ") + label + ": none of " + names
                                     + " is installed; math uses the text font",
                          std::string(label) + " not installed: the message names the families");
                    std::vector<std::string> msgs;
                    Figure::set_message_handler([&](std::string_view s) { msgs.emplace_back(s); });
                    FigureOptions fo;
                    fo.math_fontset = set;
                    auto fig = Figure::create(fo);
                    fig->add_subplot(1, 1, 1)->set_title("$x^2$");
                    (void) fig->render_svg();
                    Figure::set_message_handler(nullptr);
                    check(msgs.size() == 1 && msgs[0] == "Figure: " + msg, std::string(label) + ": one message, at create");
                }
            }
            check(resolve_math_font(MathFontset::Custom, "no/such/font.ttf", msg).empty()
                  && msg == "math_font_path 'no/such/font.ttf' cannot be read as a font; math uses the text font",
                  "Custom, unreadable: the text font and a message");
            check(resolve_math_font(MathFontset::Custom, "", msg).empty() && !msg.empty(), "Custom with no path: likewise");

            // A math font of its own: every run in a span but \text and \mathdefault.
            const FontEntry* def = pick_default_font();
            const FontEntry* other = nullptr;
            for (const FontEntry* e: fallback_fonts())
                if (e->index == 0 && def && e->path != def->path) {
                    other = e;
                    break;
                }
            if (other) {
                const std::string mf = resolve_math_font(MathFontset::Custom, other->path, msg);
                check(mf == other->path && msg.empty(), "Custom: the file itself (" + other->name + ")");
                const MathTextScope scope(true, mf);
                const RichLine l = layout_rich("", kSize, "a $12\\text{ ab}\\,\\mathdefault{c}$");
                const RichRun* a = run(l, "a ");
                const RichRun* one = run(l, "12");
                const RichRun* ab = run(l, " ab");
                check(a && a->font.empty() && one && one->font == mf && ab && ab->font.empty()
                      && run(l, "c") && run(l, "c")->font.empty(),
                      "the span in the math font; outside it, \\text and \\mathdefault in the text font");
                check(close(one ? one->width : 0.0f, text_width(mf, kSize, "1") + text_width(mf, kSize, "2"), 0.01f)
                      && l.vm.ascent >= font_vmetrics(mf, kSize).ascent - 1e-3f,
                      "measured in the math font, its extent in the line's");
                auto fig = Figure::create({.math_fontset = MathFontset::Custom, .math_font_path = mf});
                fig->add_subplot(1, 1, 1)->set_title("$y=1$");
                const std::string svg = fig->render_svg().svg;
                check(svg.find("font-family=\"'" + other->name + "',") != std::string::npos,
                      "SVG: a math run names the math font's family");
            } else {
                std::printf("  (no second font to use as a math font: Custom checks skipped)\n");
            }
            check(run(layout_rich("", kSize, "$x$"), "x")->font.empty(), "no scope: math in the text font");

            // A math font with no italic or bold face: its Unicode math letters.
            const FontEntry* math = nullptr;
            for (const char* n: {"STIX Two Math", "DejaVu Math TeX Gyre", "Noto Sans Math"})
                if (!math) math = find_family(n);
            if (math && !has_face(math->path, FontFace::Italic) && font_has_glyphs(math->path, "\xF0\x9D\x91\xA5")) {
                const MathTextScope scope(true, math->path);
                const RichLine l = layout_rich("", kSize, "$x\\alpha h$");
                check(run(l, "\xF0\x9D\x91\xA5\xF0\x9D\x9B\xBC\xE2\x84\x8E") && !l.runs[0].italic,
                      "x, alpha, h: the math italic letters (h its hole), upright, in " + math->name);
                const RichLine digit = layout_rich("", kSize, "$\\mathit{2}$");
                const RichRun* two = run(digit, "2");
                check(two && two->italic, "an italic digit (none in Unicode): slanted");
                if (!has_face(math->path, FontFace::Bold) && font_has_glyphs(math->path, "\xF0\x9D\x90\xB1"))
                    check(run(layout_rich("", kSize, "$\\mathbf{x}$"), "\xF0\x9D\x90\xB1") != nullptr,
                          "\\mathbf: the math bold letters");
            } else {
                std::printf("  (no math font without an italic face: math-letter checks skipped)\n");
            }
        }

        // --- Warnings: the new commands no longer warn -------------------------------------
        {
            std::vector<std::string> msgs;
            Figure::set_message_handler([&](std::string_view s) { msgs.emplace_back(s); });
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_title("$\\frac{1}{2}\\sqrt[3]{x}\\overline{y}\\underline{z}$");
            check(msgs.empty(), "no warning for 37a's commands");
            ax->set_title("$\\hat{x}\\widetilde{xy}\\vec{v}\\overleftarrow{AB}\\hat{\\imath}$");
            check(msgs.empty(), "no warning for 37b's accents");
            ax->set_title("$\\left(\\frac{a}{b}\\middle|\\binom{n}{k}\\right.\\bigl[x\\Biggr\\}$");
            check(msgs.empty(), "no warning for 37c's delimiters");
            ax->set_title("$\\sum_{\\substack{i\\\\j}}\\int_0^1\\lim_{x\\to 0}\\overset{a}{b}\\underset{c}{d}"
                          "\\stackrel{e}{=}\\operatorname{tr}\\bigsqcup$");
            check(msgs.empty(), "no warning for 37d's operators");
            ax->set_title("$\\mathbf{x}\\mathcal{A}\\mathscr{L}\\mathbb{R}\\mathfrak{g}\\mathsf{s}\\mathtt{t}$");
            check(msgs.empty(), "no warning for 37e's fonts");
            ax->set_title("$\\boldsymbol{x}$");
            check(msgs.size() == 1 && msgs[0].find("not supported yet") != std::string::npos,
                  "a still deferred command still warns");
            Figure::set_message_handler(nullptr);
        }

        // --- For the eye ----------------------------------------------------------------------
        {
            FigureOptions fo;
            fo.width = 900;
            fo.height = 900;
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
            ax->text("$\\frac{a+b}{c} \\quad \\dfrac{1}{2} \\quad \\frac{\\frac{1}{x}+1}{y}$", 0.03, 0.93, big);
            ax->text("$\\hat{x}\\,\\bar{x}\\,\\tilde{a}\\,\\dot{x}\\,\\ddot{y}\\,\\acute{e}\\,\\grave{a}\\,\\breve{u}"
                     "\\,\\check{c}\\,\\mathring{A}\\,\\vec{v}\\,\\hat{X}\\,\\bar{F}\\,\\hat{\\imath}\\,\\vec{x}^2$",
                     0.03, 0.71, big);
            ax->text("$\\widehat{xyz}\\quad\\widetilde{abc}\\quad\\overrightarrow{AB}\\quad\\overleftarrow{PQ}"
                     "\\quad\\widehat{i}$", 0.03, 0.61, big);
            ax->text("$\\left(\\frac{a}{b}\\right)\\left[\\frac{1}{x}\\right]\\left\\{\\frac{\\frac{a}{b}}{c}\\right\\}"
                     "\\left|\\frac{a}{b}\\right|\\left\\langle\\frac{a}{b}\\right\\rangle\\left\\lfloor\\frac{a}{b}"
                     "\\right\\rfloor\\left\\lceil\\frac{a}{b}\\right\\rceil\\left\\|\\frac{a}{b}\\right\\|$",
                     0.03, 0.48, big);
            ax->text("$\\bigl(\\Bigl(\\biggl(\\Biggl(x\\Biggr)\\biggr)\\Bigr)\\bigr)\\quad\\binom{n}{k}\\quad"
                     "\\left\\{x\\middle|\\frac{x}{2}>0\\right\\}\\quad\\left/\\frac{a}{b}\\right\\backslash"
                     "\\quad\\left.\\frac{df}{dx}\\right|_{x=0}$", 0.03, 0.36, big);
            ax->text("$\\sqrt{2}\\quad\\sqrt[3]{x}\\quad\\sqrt{\\frac{a}{b}}\\quad\\sqrt{x_i^2+y}$", 0.03, 0.82, big);
            ax->text("$\\overline{AB}\\quad\\underline{xyz}\\quad x^{\\frac{1}{2}}$\nnext line\n$\\frac{1}{2}$ below",
                     0.55, 0.86, big);
            ax->text("$\\sum_{i=0}^{n} x_i\\quad\\int_0^1 f\\,dx\\quad\\oint_C\\quad\\lim_{x\\to 0}\\frac{\\sin x}{x}"
                     "\\quad\\prod_{\\substack{0<i<n\\\\ i\\neq j}} a_i$", 0.03, 0.28, big);
            ax->text("$\\bigcup_k A_k\\,\\bigoplus_k V_k\\quad\\overset{!}{=}\\quad A\\stackrel{f}{\\to}B\\quad"
                     "\\underset{x}{\\operatorname{argmax}}\\,g\\quad\\max_x f\\quad x^{\\sum_i y}$", 0.03, 0.19, big);
            ax->text("$\\mathbf{F} = m\\mathbf{a}\\quad\\mathcal{L}\\,\\mathscr{H}\\quad\\mathbb{R}^n\\,\\mathbb{Z}"
                     "\\quad\\mathfrak{g}\\quad\\mathsf{sans}\\,\\mathtt{mono}$", 0.03, 0.09, big);
            TextOptions turned = big;
            turned.rotation = 30.0f;
            ax->text("$\\frac{1}{\\sqrt{n}}$", 0.85, 0.22, turned);
            fig->savefig("math_layout.png");
            fig->savefig("math_layout.svg");
        }
    }
} // namespace lt
