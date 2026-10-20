// Color construction, the default color cycle and the colormap registry. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "color_cycle.h"
#include "colormaps.h"
#include "figure_export.h"
#include "figure_impl.h"
#include "renderer/plane2d.h"
#include "renderer/scatter3d.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace lt {
    void test_color_from_hex() {
        std::printf("\n[Color::from_hex]\n");

        auto byte_is = [](float c, int b) { return std::fabs(c - b / 255.0f) < 1e-6f; };
        auto is = [&](const sextant::Color& c, int r, int g, int b, int a) {
            return byte_is(c.r, r) && byte_is(c.g, g) && byte_is(c.b, b) && byte_is(c.a, a);
        };

        check(is(sextant::Color::from_hex(0x112233), 0x11, 0x22, 0x33, 0xFF),
              "0xRRGGBB: channels in order, opaque");
        check(is(sextant::Color::from_hex(0x11223344), 0x11, 0x22, 0x33, 0x44),
              "0xRRGGBBAA: green and blue are not swapped, alpha last");
        check(is(sextant::Color::from_hex(0xFF000080), 0xFF, 0x00, 0x00, 0x80),
              "0xRRGGBBAA: half-transparent red");
        check(is(sextant::Color::from_hex(0x00FF00), 0x00, 0xFF, 0x00, 0xFF),
              "0xRRGGBB: pure green");

        // from_name: the string's length says whether there is alpha.
        using sextant::Color;
        check(is(Color::from_name("#112233"), 0x11, 0x22, 0x33, 0xFF), "#rrggbb");
        check(is(Color::from_name("#11223344"), 0x11, 0x22, 0x33, 0x44), "#rrggbbaa");
        check(is(Color::from_name("#000000ff"), 0x00, 0x00, 0x00, 0xFF),
              "#rrggbbaa with red 0: opaque black, not blue");
        check(is(Color::from_name("#0000ff80"), 0x00, 0x00, 0xFF, 0x80),
              "#rrggbbaa with red 0: half-transparent blue");
        check(is(Color::from_name("#00FF00"), 0x00, 0xFF, 0x00, 0xFF), "upper-case digits");
        const Color grey = Color::from_name("grey");
        check(grey.r == Color::Gray.r && grey.g == Color::Gray.g && grey.b == Color::Gray.b,
              "a name");

        auto throws = [](std::string_view s) {
            try { (void) Color::from_name(s); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        for (std::string_view bad : {"#12zz56", "#1234", "#1234567", "#", "#123456789", "#0x1234",
                                     "#-12345", "teal", ""})
            check(throws(bad), "from_name(\"" + std::string(bad) + "\") throws");
    }

    // An unset `color` takes the axes' next cycle color at the call.
    void test_color_cycle() {
        std::printf("\n[color cycle]\n");
        using namespace sextant;

        const std::vector<Color> tab10 = default_color_cycle();
        auto same = [](const std::optional<Color>& c, const Color& e) {
            return c && c->r == e.r && c->g == e.g && c->b == e.b && c->a == e.a;
        };
        check(tab10.size() == 10 && same(tab10[0], Color::Blue) && same(tab10[1], Color::Orange)
              && same(tab10[2], Color::Green) && same(tab10[3], Color::Red)
              && same(tab10[4], Color::Purple) && same(tab10[9], Color::Cyan),
              "the default is tab10, Color::Blue first, the named colors exactly");

        const std::vector<double> x{ 0.0, 1.0, 2.0 }, y{ 1.0, 3.0, 2.0 };
        const std::vector<double> grid{ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };

        auto fig = Figure::create();
        auto ax  = fig->add_subplot(1, 3, 1);
        auto ax2 = fig->add_subplot(1, 3, 2);
        auto ax3 = fig->add_subplot3d(1, 3, 3);
        auto& fi = detail::FigureAccess::impl(*fig);
        auto* a  = fi.find_slot_impl(1);
        auto* a2 = fi.find_slot_impl(2);
        auto* a3 = fi.find_slot_impl3d(3);

        // 2D: every flat-colored kind takes one; colormapped kinds take none.
        ax->line(x, y).scatter(x, y).scatter_z(x, y, y).heatmap(grid, 2, 3, {0, 3}, {0, 2})
           .bar(x, y).hist(y);
        check(same(a->lines[0].opts.color, tab10[0]) && same(a->scatters[0].opts.color, tab10[1])
              && same(a->bars[0].opts.color, tab10[2]) && same(a->bars[1].opts.color, tab10[3]),
              "2D: line, scatter, bar, hist take colors in call order; scatter_z and heatmap none");

        // A set color is kept and uses none; a call that throws uses none.
        ax->line(x, y, {.color = Color::Black});
        try { ax->line(x, std::vector<double>{ 1.0 }); } catch (const std::invalid_argument&) {}
        ax->line(x, y);
        check(same(a->lines[1].opts.color, Color::Black) && a->lines.size() == 3
              && same(a->lines[2].opts.color, tab10[4]),
              "2D: an explicit color, and a call that throws, leave the cycle where it was");

        ax2->line(x, y);
        check(same(a2->lines[0].opts.color, tab10[0]), "2D: each axes has its own cycle");

        // set_color_cycle(): restarts, wraps; empty restores tab10.
        ax->set_color_cycle({ Color::Red, Color::Green });
        ax->line(x, y).line(x, y).line(x, y);
        check(same(a->lines[3].opts.color, Color::Red) && same(a->lines[4].opts.color, Color::Green)
              && same(a->lines[5].opts.color, Color::Red),
              "2D: set_color_cycle() restarts from its first color and wraps");
        ax->set_color_cycle({});
        ax->line(x, y);
        check(same(a->lines[6].opts.color, tab10[0]), "2D: an empty cycle restores tab10");

        ax->set_color_cycle({ Color::Red });
        ax->cla();
        ax->line(x, y);
        check(same(a->lines[0].opts.color, tab10[0]), "2D: cla() resets the cycle to tab10's start");

        // 3D: colormapped objects take the next color without using it up;
        // planes share the parent's cycle, and a plane's cla() keeps it.
        const std::vector<double> u{ 0.0, 1.0, 2.0 }, v{ 0.0, 1.0 };
        const std::vector<double> h{ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
        ax3->bar3d(PlaneOrientation::XY, u, v, h);
        ax3->surface(PlaneOrientation::XY, u, v, h, {.colormap = true});
        ax3->scatter3d(x, y, y, y);
        ax3->surface(PlaneOrientation::XY, u, v, h);
        auto plane = ax3->plane(PlaneOrientation::XY, 0.5);
        plane->line(x, y);
        plane->cla();
        plane->bar(x, y);
        ax3->line3d(x, y, y);
        const auto& sheet = a3->plane_at(0).sheet;
        check(same(a3->bars3d[0].opts.color, tab10[0]) && same(a3->surfaces[0].opts.color, tab10[1])
              && same(a3->scatter3d[0].opts.color, tab10[1])
              && same(a3->surfaces[1].opts.color, tab10[1])
              && same(sheet.bars[0].opts.color, tab10[3]) && same(a3->lines3d[0].opts.color, tab10[4]),
              "3D: colormapped objects don't advance; planes share the cycle through a plane cla()");
        ax3->cla();
        ax3->scatter3d(x, y, y);
        check(same(a3->scatter3d[0].opts.color, tab10[0]), "3D: cla() resets the cycle");
    }

    // Colormap as a value: names, reversal, and maps registered at run time.
    // The registry is process-wide, so every name here starts "lt_".
    void test_colormap_registry() {
        std::printf("\n[colormap registry]\n");
        using namespace sextant;

        auto entry = [](Colormap m, int i) {
            const std::uint8_t* p = colormaps::get(m) + i * 4;
            return std::array<int, 4>{ p[0], p[1], p[2], p[3] };
        };
        auto throws = [](auto&& f) {
            try { f(); } catch (const std::invalid_argument&) { return true; }
            return false;
        };

        // Built-ins: names round-trip both ways, any case, and "_r" reverses.
        const std::vector<std::string> names = Colormap::names();
        bool round_trip = names.size() >= colormaps::builtin_count();
        for (std::size_t i = 0; i < colormaps::builtin_count() && round_trip; ++i) {
            const Colormap m = Colormap::from_name(names[i]);
            round_trip = m.id() == i && !m.is_reversed() && m.name() == names[i]
                         && Colormap::from_name(m.reversed().name()) == m.reversed();
        }
        check(round_trip && names[0] == "viridis" && Colormap{} == Colormap::Viridis,
              "built-ins: name -> map -> name, reversed too; the default is viridis");
        check(Colormap::from_name("Coolwarm_R") == Colormap::Coolwarm.reversed()
              && Colormap::Gray.reversed().reversed() == Colormap::Gray
              && Colormap::Gray.reversed() != Colormap::Gray,
              "any case; reversing twice gives the map back; a reversed map is a different value");
        check(throws([] { (void) Colormap::from_name("no_such_map"); })
              && throws([] { (void) Colormap::from_name("viridis_r_r"); })
              && throws([] { (void) Colormap::from_name("_r"); }),
              "an unknown name throws");

        bool mirrored = true;
        for (int i = 0; i < 256; ++i)
            mirrored = mirrored && entry(Colormap::Magma.reversed(), i) == entry(Colormap::Magma, 255 - i);
        check(mirrored, "a reversed map's table is the map's, end to end");

        // Registering: evenly spaced colors, linear between, alpha included.
        const Colormap ramp = register_colormap("lt_ramp", { Color::Black, Color::White });
        const Colormap fade = register_colormap("lt_fade", { Color{ 1, 0, 0, 0 }, Color{ 1, 0, 0, 1 } });
        const Colormap tri  = register_colormap("lt_tri", { Color::Black, Color{ 1, 0, 0, 1 }, Color::Black });
        check(ramp.id() >= colormaps::builtin_count() && fade.id() == ramp.id() + 1
              && ramp.name() == "lt_ramp" && Colormap::from_name("LT_RAMP_r") == ramp.reversed()
              && Colormap::names().back() == "lt_tri",
              "register_colormap: ids follow the built-ins in order; named as given, any case");
        check(entry(ramp, 0) == std::array{ 0, 0, 0, 255 } && entry(ramp, 255) == std::array{ 255, 255, 255, 255 }
              && entry(ramp, 128)[0] == 128 && entry(ramp.reversed(), 0) == std::array{ 255, 255, 255, 255 },
              "register_colormap: two colors ramp linearly end to end; reversal applies");
        check(entry(fade, 0)[3] == 0 && entry(fade, 255)[3] == 255 && entry(fade, 64)[3] == 64,
              "register_colormap: alpha interpolates too");
        check(entry(tri, 0)[0] == 0 && entry(tri, 255)[0] == 0 && entry(tri, 127)[0] >= 253
              && entry(tri, 128)[0] >= 253,
              "register_colormap: three colors spread evenly, the middle one at the middle");

        check(throws([] { register_colormap("lt_ramp", { Color::Red, Color::Blue }); })
              && throws([] { register_colormap("VIRIDIS", { Color::Red, Color::Blue }); })
              && throws([] { register_colormap("lt_bad_r", { Color::Red, Color::Blue }); })
              && throws([] { register_colormap("", { Color::Red, Color::Blue }); })
              && throws([] { register_colormap("lt_one", { Color::Red }); }),
              "register_colormap: a taken name (any case), an \"_r\" name, an empty name or "
              "one color throws");

        // Concurrent registration and lookup: every id distinct, every name found.
        std::vector<std::thread> threads;
        std::vector<Colormap> made(8);
        std::atomic<bool> stop{ false };
        std::thread reader([&] {
            while (!stop) (void) colormaps::get(ramp), (void) Colormap::names();
        });
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&, t] {
                made[t] = register_colormap("lt_thread_" + std::to_string(t), { Color::Red, Color::Blue });
            });
        for (auto& th : threads) th.join();
        stop = true;
        reader.join();
        std::set<std::uint16_t> ids;
        bool found = true;
        for (int t = 0; t < 8; ++t) {
            ids.insert(made[t].id());
            found = found && Colormap::from_name("lt_thread_" + std::to_string(t)) == made[t];
        }
        check(ids.size() == 8 && found, "register_colormap from 8 threads: distinct ids, each found by name");

        // Drawn: a reversed and a registered map reach the picture and its colorbar.
        auto svg_of = [](Colormap m) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> grid{ 0.0, 0.25, 0.5, 0.75, 1.0, 0.5 };
            ax->imshow(grid, 2, 3, { .cmap = m, .colorbar = true });
            return render_figure_svg(*detail::FigureAccess::impl(*fig).host_frame(), 400, 300);
        };
        const std::string fwd = svg_of(Colormap::Viridis);
        check(fwd != svg_of(Colormap::Viridis.reversed()) && fwd != svg_of(ramp)
              && svg_of(ramp) != svg_of(ramp.reversed()),
              "a reversed or registered map changes the drawing");
    }

    // The newer built-ins, their groups, and cyclic maps wrapping on every path.
    void test_colormap_cyclic() {
        std::printf("\n[colormaps: built-ins and cyclic wrap]\n");
        using namespace sextant;
        using colormaps::Category;

        auto entry = [](Colormap m, int i) {
            const std::uint8_t* p = colormaps::get(m) + i * 4;
            return std::array<int, 4>{ p[0], p[1], p[2], p[3] };
        };

        check(colormaps::builtin_count() == 26 && Colormap::RdBu.name() == "RdBu"
              && Colormap::from_name("rdbu") == Colormap::RdBu
              && Colormap::from_name("TWILIGHT_SHIFTED_r") == Colormap::TwilightShifted.reversed(),
              "26 built-ins, named as matplotlib spells them, found in any case");
        check(entry(Colormap::Blues, 0) == std::array{ 247, 251, 255, 255 }
              && entry(Colormap::Blues, 255) == std::array{ 8, 48, 107, 255 }
              && entry(Colormap::RdBu, 0) == std::array{ 103, 0, 31, 255 }
              && entry(Colormap::Phase, 0) == std::array{ 168, 120, 13, 255 },
              "tables match their sources at the ends (Blues, RdBu, phase)");

        bool groups = true;
        for (Colormap m : { Colormap::Twilight, Colormap::TwilightShifted, Colormap::Hsv, Colormap::Phase })
            groups = groups && m.is_cyclic() && m.reversed().is_cyclic()
                     && Colormap::from_name(m.reversed().name()).is_cyclic()
                     && colormaps::category(m) == Category::Cyclic;
        for (Colormap m : { Colormap::Coolwarm, Colormap::RdBu, Colormap::Spectral, Colormap::BrBG, Colormap::PiYG })
            groups = groups && !m.is_cyclic() && colormaps::category(m) == Category::Diverging;
        for (Colormap m : { Colormap::Viridis, Colormap::Gray, Colormap::Blues, Colormap::Jet, Colormap::Hot })
            groups = groups && !m.is_cyclic() && colormaps::category(m) == Category::Sequential;
        check(groups, "groups: four cyclic maps (reversed and by name too), five diverging, the rest sequential");
        check(entry(Colormap::Twilight, 0) == entry(Colormap::Twilight, 255)
              && entry(Colormap::Phase, 0) == entry(Colormap::Phase, 255),
              "twilight and phase: the two ends meet");

        const Colormap cyc = register_colormap("lt_cyclic", { Color::Red, Color::Blue, Color::Red }, true);
        check(cyc.is_cyclic() && Colormap::from_name("lt_cyclic_r").is_cyclic()
              && colormaps::category(cyc) == Category::Cyclic
              && !Colormap::from_name("lt_ramp").is_cyclic(),
              "register_colormap(..., true) makes a cyclic map; the default is not");

        using colormaps::fold;
        check(fold(1.25, true) == 0.25 && fold(-0.25, true) == 0.75 && fold(1.0, true) == 0.0
              && fold(1.25, false) == 1.0 && fold(-0.25, false) == 0.0
              && fold(std::nan(""), true) == 0.0 && colormaps::index(1.0, false) == 255,
              "fold(): clamps, or for a cyclic map wraps (vmax lands on vmin); NaN at the low end");

        // CPU paths: heatmap cells and a 3D point.
        HeatmapPlot hp;
        hp.rows = 1;
        hp.cols = 3;
        hp.data = std::vector<float>{ 0.25f, 1.25f, -0.75f };
        hp.opts.cmap = Colormap::Twilight;
        auto cells = plane_heatmap_rgba(hp);
        auto cell = [&](int c) { return std::vector<std::uint8_t>(cells.begin() + c * 4, cells.begin() + c * 4 + 4); };
        const bool wraps = cell(0) == cell(1) && cell(0) == cell(2);
        hp.opts.cmap = Colormap::Viridis;
        cells = plane_heatmap_rgba(hp);
        check(wraps && cell(0) != cell(1) && cell(0) != cell(2),
              "heatmap: a cyclic map gives 0.25, 1.25 and -0.75 one color; viridis clamps them apart");

        Scatter3DPlot sp;
        sp.colors = std::vector<double>{ 0.25, 1.25 };
        sp.opts.cmap = Colormap::Phase;
        const Color a = scatter3d_point_color(sp, 0, 0.0, 1.0), b = scatter3d_point_color(sp, 1, 0.0, 1.0);
        check(a.r == b.r && a.g == b.g && a.b == b.b, "scatter3d: a cyclic map wraps a point's value");

        // GPU paths: shifting every value by one period changes nothing under a
        // cyclic map (the shader wraps), and most of the picture under viridis.
        auto pixels = [](Colormap m, double shift, bool path) {
            auto fig = Figure::create({ .width = 240, .height = 200 });
            auto ax = fig->add_subplot3d(1, 1, 1);
            const std::vector<double> x{ 0.0, 1.0, 1.0, 0.0 }, y{ 0.0, 0.0, 1.0, 1.0 }, z{ 0.0, 0.0, 0.0, 0.0 };
            std::vector<double> v{ 0.0, 0.25, 0.5, 0.75 };
            for (double& e : v) e += shift;
            if (path) {
                ax->line3d(x, y, z, v, { .linewidth = 8.0f, .cmap = m, .vmin = 0.0f, .vmax = 1.0f });
            } else {
                const std::vector<std::uint32_t> tri{ 0, 1, 2, 0, 2, 3 };
                ax->surface_tri(x, y, z, tri, v, { .cmap = m, .vmin = 0.0f, .vmax = 1.0f });
            }
            return fig->render_rgba().pixels;
        };
        auto differing = [](const std::vector<std::uint8_t>& p, const std::vector<std::uint8_t>& q) {
            if (p.size() != q.size()) return p.size();
            std::size_t n = 0;
            for (std::size_t i = 0; i < p.size(); i += 4)
                for (int k = 0; k < 3; ++k)
                    if (std::abs(int(p[i + k]) - int(q[i + k])) > 2) { ++n; break; }
            return n;
        };
        for (bool path : { false, true }) {
            const char* kind = path ? "line3d" : "surface_tri";
            const std::size_t cyc_diff = differing(pixels(Colormap::Twilight, 0.0, path),
                                                   pixels(Colormap::Twilight, 1.0, path));
            const std::size_t lin_diff = differing(pixels(Colormap::Viridis, 0.0, path),
                                                   pixels(Colormap::Viridis, 1.0, path));
            std::printf("  %s: %zu px differ shifted under twilight, %zu under viridis\n", kind, cyc_diff,
                        lin_diff);
            check(cyc_diff <= 4 && lin_diff > 200,
                  std::string(kind) + " (GL): values one period up draw the same under a cyclic map");
        }
    }
} // namespace lt
