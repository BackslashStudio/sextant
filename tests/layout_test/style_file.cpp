// Style files (Figure::style_yaml / apply_style_yaml / save_style / load_style):
// a figure's look as YAML and back, across 2D, 3D and polar axes. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;

        // The same structure at any scale: a 2x2 of 2D axes, 3D bars, polar, and
        // a 2D axes with a twin. `k` changes the data only.
        std::shared_ptr<Figure> make(double k) {
            auto fig = Figure::create({.width = 480, .height = 360});
            auto a = fig->add_subplot(2, 2, 1);
            a->line(V{1, 2, 3, 4}, V{0, k, 1, 2 * k});
            a->scatter(V{1, 2, 3}, V{k, 1, 2});
            a->bar(V{1, 2, 3}, V{k, 2, 1});
            auto b = fig->add_subplot3d(2, 2, 2);
            const V u{0, 1, 2}, v{0, 1}, h{k, 1, 2, 1, k, 2};
            b->bar3d(PlaneOrientation::XY, u, v, h);
            auto c = fig->add_subplot_polar(2, 2, 3);
            c->plot(V{0, 1, 2}, V{0.5, k, 1.5});
            auto d = fig->add_subplot(2, 2, 4);
            d->line(V{1, 2, 3}, V{1, 2 * k, 3});
            auto t = d->twinx();
            t->line(V{1, 2, 3}, V{10, 20 * k, 5});
            return fig;
        }

        // The look, by hand through the setters.
        void restyle(Figure& fig) {
            fig.set_background({0.8f, 0.9f, 0.8f, 1.0f});
            fig.set_margins({20, 24, 18, 22});
            const auto cell = [&](int i) { return fig.add_subplot(2, 2, i); };
            auto a = cell(1);
            a->grid(true);
            a->legend();
            a->set_axes_style({.background = {0.95f, 0.95f, 0.8f, 1.0f}});
            LineOptions lo = a->line_options(0);
            lo.color = Color{0.1f, 0.4f, 0.8f, 1.0f};
            lo.linewidth = 4.0f;
            lo.linestyle = LineStyle::Dashed;
            lo.name = "styled";
            a->set_line_options(0, lo);
            ScatterOptions so = a->scatter_options(0);
            so.color = Color{0.9f, 0.2f, 0.2f, 1.0f};
            a->set_scatter_options(0, so);
            a->set_xscale(Scale::Log);
            auto b = fig.add_subplot3d(2, 2, 2);
            b->set_camera({.azimuth = -20.0, .elevation = 50.0});
            b->grid(false);
            auto c = fig.add_subplot_polar(2, 2, 3);
            c->set_theta_direction(-1);
            c->set_rlabel_position(45.0);
            c->grid(false);
            auto d = fig.add_subplot(2, 2, 4);
            d->set_xtick_format("%.1f");
        }

        bool same_pixels(const RgbaImage& x, const RgbaImage& y) {
            return x.width == y.width && x.height == y.height && x.pixels == y.pixels;
        }

        bool throws_invalid(const std::function<void()>& fn) {
            try { fn(); } catch (const std::invalid_argument&) { return true; } catch (...) {}
            return false;
        }

        bool throws_runtime(const std::function<void()>& fn) {
            try { fn(); } catch (const std::runtime_error&) { return true; } catch (...) {}
            return false;
        }

        std::string joined(const std::vector<std::string>& msgs) {
            std::string s;
            for (const auto& m : msgs) s += m + "\n";
            return s;
        }
    } // namespace

    void test_style_file() {
        std::printf("\n[style files: figure look as YAML]\n");

        // Style from a restyled figure A onto B (other data): B renders as A's
        // structure restyled by hand.
        {
            auto a = make(1.0);
            restyle(*a);
            const std::string text = a->style_yaml();
            check(text.rfind("sextant_style: 1", 0) == 0, "style_yaml: starts with the format version");
            check(text.find("detail: style") != std::string::npos, "style_yaml: names its detail");
            check(text.find("title:") == std::string::npos, "style level: no titles");
            check(text.find("xlim") == std::string::npos, "style level: no limits");

            auto b = make(2.0);
            const auto msgs = b->apply_style_yaml(text);
            check(msgs.empty(), "apply_style_yaml: a file made by this library applies clean");
            if (!msgs.empty()) std::printf("%s", joined(msgs).c_str());

            auto ref = make(2.0);
            restyle(*ref);
            check(same_pixels(b->render_rgba(), ref->render_rgba()),
                  "style of A onto B renders as B restyled by hand");
            check(b->style_yaml() == text, "style_yaml: applying a file then writing gives it back");
        }

        // State: titles, limits and ticks travel too.
        {
            auto a = make(1.0);
            restyle(*a);
            auto ax = a->add_subplot(2, 2, 1);
            ax->set_title("hello");
            ax->set_xtitle("time");
            ax->set_ylim(-1, 5);
            ax->set_xticks(V{1, 2, 3}, {"one", "two", "three"});
            a->suptitle("super");
            a->add_subplot_polar(2, 2, 3)->set_rmax(4.0);
            a->add_subplot3d(2, 2, 2)->set_title("three d");
            const std::string text = a->style_yaml(Figure::StyleDetail::State);
            check(text.find("detail: state") != std::string::npos, "state: names its detail");
            check(text.find("hello") != std::string::npos && text.find("three") != std::string::npos,
                  "state: titles and tick labels are written");

            auto b = make(1.0);
            const auto msgs = b->apply_style_yaml(text);
            check(msgs.empty(), "state: applies clean");
            if (!msgs.empty()) std::printf("%s", joined(msgs).c_str());
            check(same_pixels(a->render_rgba(), b->render_rgba()), "state: round-trips onto a fresh figure of the same data");
            check(b->style_yaml(Figure::StyleDetail::State) == text, "state: writes back identically");
        }

        // Files.
        {
            auto a = make(1.0);
            restyle(*a);
            const auto dir = std::filesystem::temp_directory_path();
            const std::string path = (dir / "sextant_style_test.yaml").string();
            a->save_style(path);
            auto b = make(1.0);
            const auto msgs = b->load_style(path);
            check(msgs.empty(), "load_style: applies clean");
            check(same_pixels(a->render_rgba(), b->render_rgba()), "save_style / load_style round trip");
            std::filesystem::remove(path);
            check(throws_runtime([&] { b->load_style(path); }), "load_style: a missing file throws runtime_error");
            check(throws_runtime([&] { b->save_style((dir / "no_such_dir_x" / "s.yaml").string()); }),
                  "save_style: an unwritable path throws runtime_error");
        }

        // A differently shaped figure: skipped with messages, never a throw.
        {
            auto a = make(1.0);
            restyle(*a);
            const std::string text = a->style_yaml();

            auto f = Figure::create({.width = 320, .height = 240});
            auto ax = f->add_subplot(1, 2, 1);
            ax->line(V{0, 1}, V{0, 1});
            auto three = f->add_subplot3d(1, 2, 2);
            three->bar3d(PlaneOrientation::XY, V{0, 1}, V{0, 1}, V{1, 2, 3, 4});
            std::vector<std::string> msgs;
            check(!throws_invalid([&] { msgs = f->apply_style_yaml(text); }) , "other shape: does not throw");
            const std::string all = joined(msgs);
            check(!msgs.empty(), "other shape: says what it skipped");
            check(all.find("grid: the file is for 2x2 cells, this figure has 1x2") != std::string::npos,
                  "other shape: names the grid mismatch");
            check(all.find("axes[2]: this figure has no axes at cell 3") != std::string::npos,
                  "other shape: names the missing cell");
            check(all.find("axes[0].plots.scatter: the file has 1 but this figure has 0") != std::string::npos,
                  "other shape: names the plots it lacks");
            // The cell-1 2D axes still took its style.
            check(f->add_subplot(1, 2, 1)->line_options(0).linewidth == 4.0f,
                  "other shape: what does match still applies");
        }

        // Absent = unchanged; a section is applied whole.
        {
            auto f = make(1.0);
            f->set_background({0.2f, 0.3f, 0.4f, 1.0f});
            const auto bg_line = [](const std::string& t) {
                const auto p = t.find("  background:");
                return p == std::string::npos ? std::string() : t.substr(p, t.find('\n', p) - p);
            };
            const std::string bg = bg_line(f->style_yaml());
            check(!bg.empty(), "partial file: the background is written");
            const std::string only = "sextant_style: 1\nfigure:\n  margins:\n    left: 33\n";
            const auto msgs = f->apply_style_yaml(only);
            check(msgs.empty(), "partial file: applies clean");
            check(bg_line(f->style_yaml()) == bg, "partial file: the background is untouched");
            check(f->style_yaml().find("left: 33") != std::string::npos, "partial file: margins.left set");
            check(f->style_yaml().find("top:") == std::string::npos, "partial file: the rest of the margins reset to default");
        }

        // Bad input: messages for values, a throw for the document.
        {
            auto f = make(1.0);
            const auto msgs = f->apply_style_yaml(
                "sextant_style: 1\nfigure:\n  pick_radius: -4\n  col_gap: lots\n  mystery: 1\naxes:\n"
                "  - cell: 1\n    kind: axes\n    grid: maybe\n    plots:\n      line:\n        - linestyle: wiggly\n");
            const std::string all = joined(msgs);
            check(all.find("figure.pick_radius") != std::string::npos, "bad value: pick_radius named");
            check(all.find("figure.col_gap") != std::string::npos, "bad value: col_gap named");
            check(all.find("mystery") != std::string::npos, "bad value: unknown key named");
            check(all.find("axes[0].grid") != std::string::npos, "bad value: nested path named");
            check(all.find("linestyle") != std::string::npos && all.find("wiggly") != std::string::npos,
                  "bad value: enum value named");

            check(throws_invalid([&] { f->apply_style_yaml("a: [unclosed"); }), "document: bad YAML throws");
            check(throws_invalid([&] { f->apply_style_yaml("hello: world\n"); }), "document: not a style file throws");
            check(throws_invalid([&] { f->apply_style_yaml("sextant_style: 2\n"); }), "document: a newer format throws");
            check(throws_invalid([&] { f->apply_style_yaml("sextant_style: abc\n"); }), "document: a bad version throws");
        }
    }
}
