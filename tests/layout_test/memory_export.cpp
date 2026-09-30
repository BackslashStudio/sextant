// In-memory export (v1.1 step 25): render_png(), render_svg(), render_rgba().
// The savefig_* functions are these plus a file write, so the gate is byte
// identity between a file and the in-memory output for the same figure --
// 2D and 3D gallery figures, at the default and a raised dpi, headless and
// through an open window.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_broker.h"
#include "output/png_writer.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 480, H = 360;

        std::string slurp(const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }

        std::string as_string(const std::vector<std::uint8_t>& v) {
            return std::string(v.begin(), v.end());
        }

        // Two renders of one figure agree: exactly, or where the renderer does
        // not repeat an export (Apple's software one) up to 1% of pixels by any
        // amount -- a depth-peeled translucent scene there moves hundreds of
        // pixels by up to ~50 levels between identical exports (see scene3d.cpp).
        // The exact, renderer-free check is the encode test in test_memory_export().
        bool renders_agree(int off, int w, int h) {
            return renderer_repeats_exactly() ? off == 0 : off * 100 <= w * h;
        }

        // The render_png() bytes are savefig_png()'s file -- two renders, so
        // subject to renders_agree() where the bytes differ.
        bool png_matches_file(const std::vector<std::uint8_t>& png, const std::string& file) {
            if (as_string(png) == slurp(file)) return true;
            if (renderer_repeats_exactly()) return false;
            const std::string tmp = file + ".mem.png";
            { std::ofstream(tmp, std::ios::binary) << as_string(png); }
            const PixelDiff d = png_pixel_diff(tmp, file);
            std::printf("    %s: %d px differ from memory, worst delta %d (%s)\n", file.c_str(),
                        d.px, d.worst, gl_renderer().c_str());
            return d.px >= 0 && renders_agree(d.px, d.w, d.h);
        }

        // 2D gallery: line + scatter with a legend, bars with error bars, a
        // heatmap with labelled contours and a colorbar, a colormapped scatter.
        std::shared_ptr<Figure> gallery_2d(float dpi = 96.0f) {
            auto fig = Figure::create({.width = W, .height = H, .title = "mem2d", .dpi = dpi});
            fig->suptitle("memory export");

            std::vector<double> x(60), s(60), c(60);
            for (std::size_t i = 0; i < x.size(); ++i) {
                x[i] = static_cast<double>(i) * 0.1;
                s[i] = std::sin(x[i]);
                c[i] = std::cos(x[i]);
            }
            auto a1 = fig->add_subplot(2, 2, 1);
            a1->line(x, s, {.color = Color::Blue, .name = "sin"});
            a1->scatter(x, c, {.color = Color::Red, .size = 12.0f, .name = "cos"});
            a1->set_title("line & scatter").legend();

            const std::vector<double> bx = {1, 2, 3, 4, 5}, bh = {3, 5, 2, 6, 4},
                                      be = {0.4, 0.6, 0.3, 0.8, 0.5};
            fig->add_subplot(2, 2, 2)->bar(bx, bh, ErrorBar{.y_cap_lo = be, .y_cap_hi = be},
                                           {.color = Color::Orange})
               .set_title("bars");

            constexpr int R = 24, C = 32;
            std::vector<double> cells(R * C);
            for (int r = 0; r < R; ++r)
                for (int k = 0; k < C; ++k)
                    cells[r * C + k] = 0.5 + 0.5 * std::sin(k * 0.3) * std::cos(r * 0.25);
            fig->add_subplot(2, 2, 3)->heatmap(cells, R, C, {0, 4}, {0, 3},
                                               {.colorbar = true, .name = "level",
                                                .contours = {0.3, 0.5, 0.7},
                                                .contour_labels = true})
               .set_title("heatmap");

            fig->add_subplot(2, 2, 4)->scatter_z(x, s, c, {.vmin = -1, .vmax = 1,
                                                          .colorbar = true})
               .set_title("scatter_z");
            return fig;
        }

        // 3D gallery: bars, a translucent colormapped surface and a plane
        // cutting through them, which the SVG painter has to split.
        std::shared_ptr<Figure> gallery_3d() {
            auto fig = Figure::create({.width = W, .height = H, .title = "mem3d"});
            auto ax = fig->add_subplot3d(1, 2, 1);
            const std::vector<double> u = {0, 1, 2, 3}, v = {0, 1, 2};
            std::vector<double> hgt(u.size() * v.size());
            for (std::size_t i = 0; i < hgt.size(); ++i) hgt[i] = 1.0 + static_cast<double>(i % 5);
            ax->bar3d(PlaneOrientation::XY, u, v, hgt, {.color = Color::Green, .edges = true,
                                                         .name = "bars"});
            ax->plane(PlaneOrientation::ZX, 1.0)
               ->imshow(std::vector<double>{0.1, 0.4, 0.7, 0.9, 0.3, 0.6}, 2, 3);
            ax->set_title("bar3d + plane").legend();

            auto sx = fig->add_subplot3d(1, 2, 2);
            std::vector<double> gu(12), gv(10), z(gu.size() * gv.size());
            for (std::size_t i = 0; i < gu.size(); ++i) gu[i] = static_cast<double>(i) * 0.5;
            for (std::size_t j = 0; j < gv.size(); ++j) gv[j] = static_cast<double>(j) * 0.5;
            for (std::size_t i = 0; i < gu.size(); ++i)
                for (std::size_t j = 0; j < gv.size(); ++j)
                    z[i * gv.size() + j] = std::sin(gu[i]) * std::cos(gv[j]);
            sx->surface(PlaneOrientation::XY, gu, gv, z,
                        {.colormap = true, .colorbar = true, .alpha = 0.7f});
            sx->set_title("surface");
            return fig;
        }

        // One figure through every entry point, file against memory.
        void check_figure(Figure& fig, const std::string& stem, PngExportOptions po = {},
                          SvgExportOptions so = {}) {
            fig.savefig_png(stem + ".png", po);
            const std::vector<std::uint8_t> png = fig.render_png(po);
            check(png_matches_file(png, stem + ".png"),
                  stem + ": render_png() is savefig_png()'s file, byte for byte");

            const SvgSaveReport file_rep = fig.savefig_svg(stem + ".svg", so);
            const SvgRender mem = fig.render_svg(so);
            check(!mem.svg.empty() && mem.svg == slurp(stem + ".svg"),
                  stem + ": render_svg() is savefig_svg()'s file, byte for byte");
            check(mem.svg.find('\r') == std::string::npos,
                  stem + ": with LF line ends on every platform");
            check(mem.report.scene_order_exact == file_rep.scene_order_exact
                      && mem.report.splits == file_rep.splits
                      && mem.report.tests == file_rep.tests
                      && mem.report.warning == file_rep.warning,
                  stem + ": and the same SvgSaveReport");

            // render_rgba() is the PNG before encoding.
            const RgbaImage rgba = fig.render_rgba(po);
            check(rgba.width > 0 && rgba.height > 0
                      && rgba.pixels.size() == static_cast<std::size_t>(rgba.width) * rgba.height * 4,
                  stem + ": render_rgba() is width x height x 4 bytes, unpadded");
            int w = 0, h = 0, comp = 0;
            unsigned char* dec = stbi_load_from_memory(png.data(), static_cast<int>(png.size()),
                                                       &w, &h, &comp, 4);
            const bool dims = dec && w == rgba.width && h == rgba.height;
            check(dims, stem + ": the PNG decodes to render_rgba()'s size");
            if (dims) {
                int off = 0, worst = 0;
                for (std::size_t i = 0; i < rgba.pixels.size(); i += 4) {
                    int m = 0;
                    for (int k = 0; k < 4; ++k)
                        m = std::max(m, std::abs(int(dec[i + k]) - int(rgba.pixels[i + k])));
                    if (m) { ++off; worst = std::max(worst, m); }
                }
                check(renders_agree(off, w, h),
                      stem + ": to render_rgba()'s pixels, top row first ("
                          + std::to_string(off) + " px differ, worst " + std::to_string(worst) + ")");
            }
            if (dec) stbi_image_free(dec);
        }
    } // namespace

    void test_memory_export() {
        std::printf("\n[memory export]\n");

        auto g2 = gallery_2d();
        check_figure(*g2, "mem_gallery2d");

        // A raised dpi scales the in-memory image exactly as it scales the file.
        check_figure(*g2, "mem_gallery2d_192", {.dpi = 192.0f});
        const RgbaImage big = g2->render_rgba({.dpi = 192.0f});
        check(big.width == 2 * W && big.height == 2 * H,
              "memory: render_rgba() at dpi 192 is twice the figure's size");
        const RgbaImage fig192 = gallery_2d(192.0f)->render_rgba();
        check(fig192.width == 2 * W && fig192.height == 2 * H,
              "memory: and FigureOptions::dpi is its default, as for a file");
        const RgbaImage sized = g2->render_rgba({}, 200, 150);
        check(sized.width == 200 && sized.height == 150,
              "memory: explicit width/height as for savefig_png()");

        auto g3 = gallery_3d();
        check_figure(*g3, "mem_gallery3d");
        check_figure(*g3, "mem_gallery3d_peel", {.peel_layers = 2});
        // A bound that binds: the report still agrees, file and memory.
        check_figure(*g3, "mem_gallery3d_bound", {}, {.max_splits = 1});

        // One render, two encodings: the PNG half of the rule with no second
        // render in it, so exact on every renderer.
        {
            const RgbaImage px = g3->render_rgba({.peel_layers = 2});
            write_png("mem_encode.png", px.width, px.height, px.pixels);
            check(as_string(write_png_to_memory(px.width, px.height, px.pixels))
                      == slurp("mem_encode.png"),
                  "memory: one image encoded to memory and to a file is the same bytes");
        }

        // savefig() dispatches to the same bytes.
        g2->savefig("mem_dispatch.svg");
        check(slurp("mem_dispatch.svg") == g2->render_svg().svg,
              "memory: savefig(\".svg\") writes render_svg()'s bytes");

        // A figure with nothing on it renders rather than throwing.
        auto empty = Figure::create({.width = 64, .height = 48});
        check(!empty->render_png().empty() && !empty->render_svg().svg.empty(),
              "memory: an empty figure renders (one implicit axes)");

        // A path that cannot be written throws, after the render, as before.
        auto throws_system = [](auto&& f) {
            try { f(); } catch (const std::system_error&) { return true; }
            return false;
        };
        check(throws_system([&] { g2->savefig_png("no_such_dir/x.png"); }),
              "memory: savefig_png() to an unwritable path throws std::system_error");
        check(throws_system([&] { g2->savefig_svg("no_such_dir/x.svg"); }),
              "memory: savefig_svg() to an unwritable path throws std::system_error");
        // The code is an errno, so in generic_category: system_category would
        // read it as a Win32 error on Windows. (ENOENT equals Win32's
        // ERROR_FILE_NOT_FOUND, so the value alone would not tell.)
        std::error_code code;
        try { g2->savefig_png("no_such_dir/x.png"); } catch (const std::system_error& e) { code = e.code(); }
        check(code.category() == std::generic_category()
                  && code == std::errc::no_such_file_or_directory,
              "memory: a missing directory is errc::no_such_file_or_directory, generic category");
    }

    // Through an open window's GL context rather than a headless one. The
    // export runs on a worker thread, so on macOS this thread can pump for it.
    void test_memory_export_windowed() {
        std::printf("\n[memory export: open window]\n");

        auto g = gallery_2d();
        g->show(false);
        std::atomic<bool> done{false};
        std::string err;
        std::thread worker([&] {
            try {
                check_figure(*g, "mem_windowed");
            } catch (const std::exception& e) {
                err = e.what();
            }
            done.store(true);
        });
        if (pump_runs_here()) pump_until([&done] { return done.load(); }, 120.0);
        worker.join();
        check(err.empty(), "memory windowed: no exception (" + err + ")");
        g->close();

        // The window's context draws the headless picture.
        check(same_picture("mem_windowed.png", "mem_gallery2d.png"),
              "memory windowed: the same picture as the headless export");
    }
} // namespace lt
