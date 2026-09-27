// The stand-in for a Python extension module: sextant_static linked into a
// shared module, driven through one exported C function. Exercises what the
// bundled dependencies serve -- FreeType for text, libpng (and zlib under it)
// for PNG -- in both a 2D and a 3D cell, and checks the files it wrote.
#include <sextant/sextant.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#if defined(_WIN32)
#define PROBE_EXPORT extern "C" __declspec(dllexport)
#else
#define PROBE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
    bool starts_with_bytes(const std::filesystem::path& p, const std::string& magic) {
        std::ifstream f(p, std::ios::binary);
        std::string head(magic.size(), '\0');
        f.read(head.data(), static_cast<std::streamsize>(head.size()));
        return f && head == magic;
    }
} // namespace

// Returns 0 on success; prints what failed otherwise.
PROBE_EXPORT int sextant_module_probe(const char* out_dir) {
    namespace fs = std::filesystem;
    try {
        constexpr int N = 200;
        std::vector<double> x(N), y(N);
        for (int i = 0; i < N; ++i) {
            x[i] = i * 2.0 * std::numbers::pi / N;
            y[i] = std::sin(x[i]);
        }

        constexpr int G = 24;
        std::vector<double> u(G), v(G), h(G * G);
        for (int i = 0; i < G; ++i) u[i] = v[i] = -2.0 + 4.0 * i / (G - 1);
        for (int i = 0; i < G; ++i)
            for (int j = 0; j < G; ++j)
                h[i * G + j] = std::exp(-(u[i] * u[i] + v[j] * v[j]));

        auto fig = sextant::Figure::create({.width = 900, .height = 400, .title = "module probe"});
        fig->add_subplot(1, 2, 1)
                ->line(x, y, {.name = "sin"})
                .set_title("2D, in a module")
                .set_xtitle("x")
                .legend();
        fig->add_subplot3d(1, 2, 2)
                ->surface(sextant::PlaneOrientation::XY, u, v, h, {.colormap = true})
                .set_title("3D, in a module");

        const fs::path dir(out_dir);
        const fs::path png = dir / "module_probe.png";
        const fs::path svg = dir / "module_probe.svg";
        fig->savefig(png.string());
        fig->savefig(svg.string());

        int failures = 0;
        if (!starts_with_bytes(png, "\x89PNG\r\n\x1a\n")) {
            std::fprintf(stderr, "FAIL: %s is not a PNG\n", png.string().c_str());
            ++failures;
        }
        if (!fs::exists(svg) || fs::file_size(svg) < 1000) {
            std::fprintf(stderr, "FAIL: %s is missing or nearly empty\n", svg.string().c_str());
            ++failures;
        }
        if (failures == 0)
            std::printf("module probe: wrote %s (%ju bytes) and %s (%ju bytes)\n",
                        png.string().c_str(), static_cast<std::uintmax_t>(fs::file_size(png)),
                        svg.string().c_str(), static_cast<std::uintmax_t>(fs::file_size(svg)));
        return failures;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
