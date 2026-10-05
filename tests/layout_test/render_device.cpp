// RenderDevice (GUI-kit R5): drawing on a GL context someone else made current,
// without touching which context is current, GLAD's table or the caller's
// framebuffer; and several figures through one device, each with its own
// DataRenderer. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_broker.h"
#include "renderer/plot_fbo.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 320, H = 240;

        // One 2D axes: a heatmap with labelled contours (traced and cached per
        // view) under a line. `phase` makes two figures differ everywhere.
        FigureSnapshot contour_figure(double phase) {
            HeatmapOptions o;
            o.contours = {-0.5, 0.0, 0.5};
            o.contour_labels = true;
            RenderSnapshot rs;
            rs.heatmaps.push_back(make_heatmap(16, 20, [phase](int r, int c) {
                return static_cast<float>(std::sin(0.4 * c + phase) * std::cos(0.3 * r));
            }, o));
            LinePlot lp;
            lp.x = CowVec<double>(std::vector<double>{0.0, 10.0, 20.0});
            lp.y = CowVec<double>(std::vector<double>{2.0, 14.0, 6.0 + phase});
            rs.lines.push_back(std::move(lp));
            FigureSnapshot fs;
            fs.axes.push_back({{1, 1, 1}, std::move(rs)});
            fs.generation = fs.data_generation = next_snapshot_generation();
            return fs;
        }

        // The same picture: byte-identical where the renderer repeats itself,
        // else at most 0.1% of pixels off by at most 8 levels (same_picture()).
        // On a mismatch it prints how many pixels differ and by how much, unless
        // `quiet` (a control that expects one).
        bool same_rgba(const RgbaImage& a, const RgbaImage& b, bool quiet = false) {
            if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size()) {
                if (quiet) return false;
                std::printf("    sizes differ: %dx%d vs %dx%d\n", a.width, a.height, b.width, b.height);
                return false;
            }
            std::size_t off = 0;
            int worst = 0;
            for (std::size_t i = 0; i < a.pixels.size(); i += 4) {
                int px = 0;
                for (int c = 0; c < 4; ++c)
                    px = std::max(px, std::abs(int(a.pixels[i + c]) - int(b.pixels[i + c])));
                if (px > 0) ++off;
                worst = std::max(worst, px);
            }
            const std::size_t n = a.pixels.size() / 4;
            const bool same = renderer_repeats_exactly() ? off == 0
                                                         : worst <= 8 && off * 1000 <= n;
            if (!same && !quiet)
                std::printf("    %zu of %zu pixels differ, by up to %d levels\n", off, n, worst);
            return same;
        }

        int loader_calls = 0;
        void* counting_loader(const char*) {
            ++loader_calls;
            return nullptr;
        }

        struct GLTarget {
            int draw = 0, read = 0;
            int vp[4] = {0, 0, 0, 0};
        };
        GLTarget gl_target() {
            GLTarget t;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &t.draw);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &t.read);
            glGetIntegerv(GL_VIEWPORT, t.vp);
            return t;
        }
        bool same_target(const GLTarget& a, const GLTarget& b) {
            return a.draw == b.draw && a.read == b.read && a.vp[0] == b.vp[0] && a.vp[1] == b.vp[1]
                && a.vp[2] == b.vp[2] && a.vp[3] == b.vp[3];
        }
    } // namespace

    // -------------------------------------------------------------------------
    // On someone else's context
    // -------------------------------------------------------------------------
    void test_render_device_host_context() {
        std::printf("\n[RenderDevice: on a context it did not make]\n");

        const FigureSnapshot fs = contour_figure(0.0);
        const int base = live_window_count();
        {
            GLContext a({.width = 64, .height = 64, .title = "host a", .visible = false});
            GLContext b({.width = 64, .height = 64, .title = "host b", .visible = false});
            // b, made last, is current; the "host" switches to a.
            a.make_current();
            check(glfwGetCurrentContext() == a.window(),
                  "host context: (setup) the host made its own context current");

            // GLAD is loaded (by a): a second load_gl() calls no loader at all.
            loader_calls = 0;
            check(load_gl(&counting_loader) && loader_calls == 0,
                  "load_gl: with the table already loaded, it returns true and loads nothing");

            RgbaImage on_a;
            {
                RenderDevice dev;
                DataRenderer data;
                check(glfwGetCurrentContext() == a.window(),
                      "host context: building a RenderDevice leaves the host's context current");
                on_a = render_figure_rgba(dev, data, fs, W, H, 2);
                check(glfwGetCurrentContext() == a.window(),
                      "host context: and so does rendering through it");
            }
            check(glGetError() == GL_NO_ERROR,
                  "host context: destroyed before the context, it leaves no GL error");

            b.make_current();
            RgbaImage on_b;
            {
                RenderDevice dev;
                DataRenderer data;
                on_b = render_figure_rgba(dev, data, fs, W, H, 2);
            }
            check(same_rgba(on_a, on_b),
                  "host context: the picture is the same as on another context of its own");
        }
        check(live_window_count() == base, "host context: both windows are gone afterwards");

        // A headless context: the same lifetime rule, and nothing left behind.
        {
            GLContext h({.width = W, .height = H, .title = "layout_test", .visible = false,
                         .resizable = false, .headless = true});
            {
                RenderDevice dev;
                DataRenderer data;
                (void)render_figure_rgba(dev, data, fs, W, H, 1);
            }
            check(glGetError() == GL_NO_ERROR,
                  "headless: a RenderDevice destroyed before its context leaves no GL error");
        }
        check(live_window_count() == base, "headless: and nothing in the broker");
    }

    // -------------------------------------------------------------------------
    // Two figures through one device
    // -------------------------------------------------------------------------
    void test_render_device_two_figures() {
        std::printf("\n[RenderDevice: two figures, one device]\n");

        const FigureSnapshot f1 = contour_figure(0.0);
        const FigureSnapshot f2 = contour_figure(1.3);

        GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});

        // Each alone, on a fresh device.
        RgbaImage alone1, alone2;
        {
            RenderDevice dev;
            DataRenderer d;
            alone1 = render_figure_rgba(dev, d, f1, W, H, 1);
        }
        {
            RenderDevice dev;
            DataRenderer d;
            alone2 = render_figure_rgba(dev, d, f2, W, H, 1);
        }
        check(!same_rgba(alone1, alone2, true), "two figures: (control) the two figures differ");

        RenderDevice dev;
        {
            // One DataRenderer per figure, as one per view.
            DataRenderer d1, d2;
            bool ok = true;
            for (int round = 0; round < 3; ++round) {
                ok = ok && same_rgba(render_figure_rgba(dev, d1, f1, W, H, 1), alone1);
                ok = ok && same_rgba(render_figure_rgba(dev, d2, f2, W, H, 1), alone2);
            }
            check(ok, "two figures: alternating through one device, each with its own "
                      "DataRenderer, each draws as it does alone");
        }
        {
            // One DataRenderer for both: the caches compare process-wide
            // generations, so it is still right, only re-traced every frame.
            DataRenderer shared;
            bool ok = true;
            for (int round = 0; round < 3; ++round) {
                ok = ok && same_rgba(render_figure_rgba(dev, shared, f1, W, H, 1), alone1);
                ok = ok && same_rgba(render_figure_rgba(dev, shared, f2, W, H, 1), alone2);
            }
            check(ok, "two figures: even one DataRenderer shared by both draws each correctly");
        }
    }

    // -------------------------------------------------------------------------
    // The caller's framebuffer and viewport
    // -------------------------------------------------------------------------
    void test_render_device_caller_target() {
        std::printf("\n[RenderDevice: the caller's framebuffer and viewport]\n");

        GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});
        RenderDevice dev;
        DataRenderer data;
        const FigureSnapshot fs = contour_figure(0.5);

        // The host renders inside a framebuffer of its own.
        GLuint host_fbo = 0, host_tex = 0;
        glGenFramebuffers(1, &host_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, host_fbo);
        glGenTextures(1, &host_tex);
        glBindTexture(GL_TEXTURE_2D, host_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 96, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, host_tex, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glViewport(3, 4, 50, 60);
        const GLTarget host = gl_target();
        check(host.draw == static_cast<int>(host_fbo) && host.read == static_cast<int>(host_fbo),
              "caller target: (setup) the host's framebuffer is bound");

        (void)render_figure_rgba(dev, data, fs, W, H, 2);
        check(same_target(gl_target(), host),
              "caller target: an export leaves the host's framebuffer and viewport bound");

        {
            PlotFbo plot;
            plot.ensure_size(100, 80, 2);
            check(same_target(gl_target(), host),
                  "caller target: allocating a PlotFbo leaves them too");
            plot.bind();
            check(gl_target().draw != static_cast<int>(host_fbo),
                  "caller target: (control) bind() does bind the plot's own target");
            render_frame(dev, data, fs, 100, 80, 2.0f);
            plot.unbind();
            check(same_target(gl_target(), host), "caller target: unbind() restores the host's");
            plot.resolve();
            check(same_target(gl_target(), host),
                  "caller target: so does a supersampled resolve(), which draws");

            plot.bind();   // resolve() with the plot still bound unbinds it
            render_frame(dev, data, fs, 100, 80, 2.0f);
            plot.resolve();
            check(same_target(gl_target(), host),
                  "caller target: resolve() straight after drawing restores the host's as well");

            PlotFbo single;
            single.ensure_size(100, 80, 1);
            single.bind();
            render_frame(dev, data, fs, 100, 80, 1.0f);
            single.resolve();
            check(same_target(gl_target(), host),
                  "caller target: and so does one with nothing to resolve");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &host_fbo);
        glDeleteTextures(1, &host_tex);
        check(glGetError() == GL_NO_ERROR, "caller target: no GL error along the way");
    }
} // namespace lt
