#include <sextant/sextant.h>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <numbers>

int main() {
    constexpr int N = 200;
    std::vector<double> x(N), y_sin(N), y_cos(N);
    for (int i = 0; i < N; ++i) {
        x[i]     = i * 2.0 * std::numbers::pi / N;
        y_sin[i] = std::sin(x[i]);
        y_cos[i] = std::cos(x[i]);
    }

    auto fig = sextant::Figure::create({.width=900, .height=400, .title="Smoke Test"});
    auto ax  = fig->axes();
    ax->line(x, y_sin, {.color=sextant::Color::Blue,  .label="sin"});
    ax->line(x, y_cos, {.color=sextant::Color::Red,
                        .linestyle=sextant::LineStyle::Dashed, .label="cos"});
    ax->set_title("Trig functions")
      .set_xtitle("x")
      .set_ytitle("amplitude")
      .grid()
      .legend();

    // Headless PNG (no display needed). Written to the current working
    // directory, like every other test's output — this used to be
    // "/tmp/sextant_smoke.png", which on Windows resolves to <drive>:\tmp\ and
    // aborted the process (0xC0000409) whenever that directory didn't exist.
    fig->savefig("sextant_smoke.png");

    // Interactive test only when a display is available
    if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY")) {
        fig->show(true);
    }
}
