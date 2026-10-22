// imshow_rgb(): an RGB/RGBA image is a heatmap whose cells carry their own
// colour; and NaN is a transparent cell in every heatmap (raster, SVG pixels,
// a perspective plane's cells, contours, hover). Part of sextant_layout_test;
// see layout_test.h.
#include "layout_test.h"
#include "heatmap_pixels.h"
#include "plot_data_view.h"

namespace lt {
    using namespace sextant;

    namespace {
        // Where the pixels within 6 of (r, g, b) are: their count and centre.
        struct Blob {
            std::size_t n = 0;
            double x = 0.0, y = 0.0;
        };

        Blob blob(const RgbaImage& im, int r, int g, int b) {
            Blob out;
            for (int y = 0; y < im.height; ++y)
                for (int x = 0; x < im.width; ++x) {
                    const std::uint8_t* p = &im.pixels[(static_cast<std::size_t>(y) * im.width + x) * 4];
                    if (std::abs(p[0] - r) <= 6 && std::abs(p[1] - g) <= 6 && std::abs(p[2] - b) <= 6) {
                        ++out.n;
                        out.x += x;
                        out.y += y;
                    }
                }
            if (out.n) {
                out.x /= static_cast<double>(out.n);
                out.y /= static_cast<double>(out.n);
            }
            return out;
        }

        // 2 x 2, row 0 first: red, green / blue, white.
        const std::vector<double> kRgb{1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1};
        const std::vector<std::uint8_t> kBytes{255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

        // A grey plot area, so white and transparent cells are told apart.
        RgbaImage render_of(const auto& draw) {
            auto fig = Figure::create({.width = 300, .height = 300});
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_axes_style({.background = {0.5f, 0.5f, 0.5f, 1.0f}});
            draw(*ax);
            return fig->render_rgba();
        }

        bool throws(const auto& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        std::vector<float> floats(const std::vector<double>& v) {
            std::vector<float> out(v.size());
            for (std::size_t i = 0; i < v.size(); ++i) out[i] = static_cast<float>(v[i]);
            return out;
        }

        HeatmapPlot image_plot(std::vector<float> data, int rows, int cols, int channels) {
            HeatmapPlot hp;
            hp.data = std::move(data);
            hp.rows = rows;
            hp.cols = cols;
            hp.xrange = {0.0, static_cast<double>(cols)};
            hp.yrange = {0.0, static_cast<double>(rows)};
            hp.channels = channels;
            return hp;
        }

        bool rgba_is(const HeatmapPixels& px, std::size_t k, int r, int g, int b, int a) {
            std::uint8_t c[4];
            px.rgba(k, c);
            return c[0] == r && c[1] == g && c[2] == b && c[3] == a;
        }
    } // namespace

    void test_heatmap_rgb() {
        std::printf("\n[imshow_rgb: images; NaN cells]\n");

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

        // ---- The pixels every output shares.
        {
            const HeatmapPlot img = image_plot({1.5f, -0.2f, 0.5f, kNaN, 0.0f, 0.0f}, 1, 2, 3);
            const HeatmapPixels px(img);
            check(rgba_is(px, 0, 255, 0, 128, 255), "pixels: an image's channels, clamped to [0, 1], opaque");
            check(rgba_is(px, 1, 0, 0, 0, 0), "pixels: a NaN channel makes the pixel transparent");

            const HeatmapPlot rgba = image_plot({0.0f, 0.0f, 1.0f, 0.5f}, 1, 1, 4);
            check(rgba_is(HeatmapPixels(rgba), 0, 0, 0, 255, 128), "pixels: RGBA keeps its own alpha");

            HeatmapOptions o;
            const HeatmapPlot hm = make_heatmap(1, 3, [&](int, int c) {
                return c == 0 ? kNaN : c == 1 ? -1.0f : 2.0f;
            }, o);
            const HeatmapPixels hpx(hm);
            const std::uint8_t* lut = colormaps::get(o.cmap);
            check(rgba_is(hpx, 0, 0, 0, 0, 0), "pixels: a NaN heatmap value is a transparent cell");
            check(rgba_is(hpx, 1, lut[0], lut[1], lut[2], 255) && rgba_is(hpx, 2, lut[1020], lut[1021], lut[1022], 255),
                  "pixels: finite values still clamp to the colormap's ends");
        }

        // ---- Raster: orientation, extent, bytes, alpha, NaN.
        {
            const RgbaImage up = render_of([](Axes& ax) { ax.imshow_rgb(kRgb, 2, 2, 3); });
            const Blob red = blob(up, 255, 0, 0), green = blob(up, 0, 255, 0), blue = blob(up, 0, 0, 255);
            check(red.n > 1000 && green.n > 1000 && blue.n > 1000 && blob(up, 255, 255, 255).n > 1000,
                  "raster: every pixel is drawn in its own colour");
            check(red.y < blue.y && red.x < green.x,
                  "raster: origin \"upper\" by default, row 0 at the top as a photo");

            const RgbaImage low = render_of([](Axes& ax) { ax.imshow_rgb(kRgb, 2, 2, 3, {.origin = "lower"}); });
            check(blob(low, 255, 0, 0).y > blob(low, 0, 0, 255).y, "raster: origin \"lower\" puts row 0 at the bottom");

            const RgbaImage bytes = render_of([](Axes& ax) { ax.imshow_rgb(kBytes, 2, 2, 3); });
            check(bytes.pixels == up.pixels, "raster: bytes draw the same picture as [0, 1] doubles");

            const RgbaImage half = render_of([](Axes& ax) {
                ax.set_xlim(0, 4).set_ylim(0, 2);
                ax.imshow_rgb(kRgb, 2, 2, 3, {.xrange = Range{0.0, 1.0}, .yrange = Range{0.0, 2.0}});
            });
            const RgbaImage full = render_of([](Axes& ax) {
                ax.set_xlim(0, 4).set_ylim(0, 2);
                ax.imshow_rgb(kRgb, 2, 2, 3, {.xrange = Range{0.0, 2.0}, .yrange = Range{0.0, 2.0}});
            });
            const double ratio = static_cast<double>(blob(half, 255, 0, 0).n) / static_cast<double>(blob(full, 255, 0, 0).n);
            check(ratio > 0.45 && ratio < 0.55, "raster: ImageOptions ranges place the image (half the width, half the pixels)");

            // Alpha 0 shows the grey; alpha 0.5 red over it is (191, 64, 64).
            const RgbaImage a = render_of([](Axes& ax) {
                ax.imshow_rgb(std::vector<double>{1, 0, 0, 0, 1, 0, 0, 0.5}, 1, 2, 4);
            });
            check(blob(a, 191, 64, 64).n > 1000 && blob(a, 255, 0, 0).n == 0,
                  "raster: RGBA alpha blends with what is under it");
            check(blob(a, 128, 128, 128).n > blob(render_of([](Axes& ax) {
                      ax.imshow_rgb(std::vector<double>{1, 0, 0, 1, 1, 0, 0, 1}, 1, 2, 4);
                  }), 128, 128, 128).n + 1000,
                  "raster: and alpha 0 leaves the plot area showing");

            const RgbaImage nan = render_of([](Axes& ax) {
                ax.imshow(std::vector<double>{std::nan(""), 1.0}, 1, 2);
            });
            const std::uint8_t* lut = colormaps::get(Colormap::Viridis);
            const Blob top = blob(nan, lut[1020], lut[1021], lut[1022]);
            const Blob bottom = blob(nan, lut[0], lut[1], lut[2]);
            const Blob grey = blob(nan, 128, 128, 128);
            check(top.n > 1000 && bottom.n == 0 && grey.n > top.n / 2 && grey.x < top.x,
                  "raster: a NaN heatmap cell is transparent, not the colormap's low end");
        }

        // ---- SVG: an image is one <image>, no colorbar.
        {
            auto fig = Figure::create({.width = 300, .height = 300});
            auto ax = fig->add_subplot(1, 1, 1);
            ax->imshow_rgb(kRgb, 2, 2, 3);
            const std::string svg = fig->render_svg().svg;
            std::size_t images = 0;
            for (std::size_t i = svg.find("<image"); i != std::string::npos; i = svg.find("<image", i + 1)) ++images;
            check(images == 1, "SVG: an image is one embedded <image>");
        }

        // ---- What only a colormapped heatmap has: colorbar, contours, cell edits.
        {
            RenderSnapshot s;
            HeatmapPlot img = image_plot(floats(kRgb), 2, 2, 3);
            img.opts.colorbar = true;
            img.opts.contours = {0.5};
            s.heatmaps.push_back(img);
            check(find_colorbar_requests(s).empty(), "image: never asks for a colorbar");
            s.heatmaps[0].channels = 1;
            s.heatmaps[0].data = std::vector<float>{0, 1, 0, 1};
            check(find_colorbar_requests(s).size() == 1, "  (the same options on a heatmap do)");
            check(trace_contours(img).empty(), "image: traces no contours");

            PlotCellEdit e;
            e.kind = PlotKind::Heatmap;
            e.plot_index = 0;
            e.column = 0;
            e.element = 0;
            e.value = 0.25;
            RenderSnapshot t;
            t.heatmaps.push_back(img);
            check(!apply_plot_data_op(t, e) && t.heatmaps[0].data[0] == 1.0f, "image: takes no cell edit");

            const auto tables = collect_plot_data_tables(t);
            check(tables.size() == 1 && tables[0].label == "image 0", "image: its Data tab is named an image");
        }

        // ---- Contours step around NaN.
        {
            HeatmapOptions o;
            o.contours = {0.5};
            const auto ramp = [](int, int c) { return static_cast<float>(c); };
            HeatmapPlot hm = make_heatmap(2, 3, ramp, o);
            check(trace_contours(hm).size() == 1, "contours: a ramp crosses 0.5 once");
            hm.data.mut()[0] = kNaN;
            check(trace_contours(hm).empty(), "contours: a square with a NaN corner is left out");
        }

        // ---- Hover.
        {
            RenderSnapshot s;
            s.heatmaps.push_back(image_plot(floats(kRgb), 2, 2, 3));
            s.heatmaps.push_back(image_plot({0.0f, 0.5f, 1.0f, 0.25f}, 1, 1, 4));
            HeatmapOptions o;
            s.heatmaps.push_back(make_heatmap(1, 2, [&](int, int c) { return c == 0 ? kNaN : 0.5f; }, o));
            PickHit h;
            h.kind = PickKind::Heatmap;
            h.row = 0;
            h.col = 1;
            h.element = 1;
            check(format_pick(s, h) == "row=0, col=1, rgb=(0, 1, 0)", "hover: an RGB pixel names its channels");
            h.object = 1;
            h.col = 0;
            h.element = 0;
            check(format_pick(s, h) == "row=0, col=0, rgba=(0, 0.5, 1, 0.25)", "hover: and RGBA its alpha");
            h.object = 2;
            check(format_pick(s, h) == "row=0, col=0, no value", "hover: a NaN cell has no value");
        }

        // ---- Read-back, set data, refusals.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->imshow_rgb(kRgb, 2, 2, 3);
            ax->imshow_rgb(kBytes, 2, 2, 3, {.xrange = Range{10.0, 20.0}});
            ax->imshow(std::vector<double>{0, 1, 2, 3}, 2, 2);
            check(ax->heatmap_count() == 3, "read-back: an image counts as a heatmap");
            const HeatmapData d = ax->heatmap_data(0);
            check(d.channels == 3 && d.rows == 2 && d.cols == 2 && d.data == kRgb &&
                      d.xrange.lo == 0.0 && d.xrange.hi == 2.0 && d.yrange.hi == 2.0,
                  "read-back: channels, shape, pixels and the index extent");
            const HeatmapData b = ax->heatmap_data(1);
            bool near = b.data.size() == kBytes.size();
            for (std::size_t i = 0; near && i < kBytes.size(); ++i)
                near = std::abs(b.data[i] - kBytes[i] / 255.0) < 1e-7;
            check(near && b.xrange.lo == 10.0 && b.xrange.hi == 20.0 && b.yrange.hi == 2.0,
                  "read-back: bytes come back / 255, with the ranges given");
            check(ax->heatmap_data(2).channels == 1, "read-back: a heatmap is channels 1");

            check(!throws([&] { ax->set_heatmap_data(0, d); }), "set data: a read-back round-trips");
            HeatmapData one = d;
            one.channels = 1;
            check(throws([&] { ax->set_heatmap_data(0, one); }), "set data: an image stays an image");
            HeatmapData three = ax->heatmap_data(2);
            three.channels = 3;
            check(throws([&] { ax->set_heatmap_data(2, three); }), "set data: a heatmap stays a heatmap");
            check(!throws([&] { ax->set_heatmap_data(0, std::vector<double>(3 * 4 * 3, 0.5), 3, 4, {0, 4}, {0, 3}); }) &&
                      ax->heatmap_data(0).rows == 3 && ax->heatmap_data(0).channels == 3,
                  "set data: the span form reshapes an image, channels kept");
            check(throws([&] { ax->set_heatmap_data(0, std::vector<double>(12, 0.5), 3, 4, {0, 4}, {0, 3}); }),
                  "set data: and wants rows x cols x channels values");

            check(throws([&] { ax->imshow_rgb(kRgb, 2, 2, 2); }) && throws([&] { ax->imshow_rgb(kRgb, 2, 2, 1); }),
                  "imshow_rgb: channels must be 3 or 4");
            check(throws([&] { ax->imshow_rgb(kRgb, 2, 2, 4); }), "imshow_rgb: data too small throws");
            check(throws([&] { ax->imshow_rgb(kRgb, 2, 2, 3, {.origin = "middle"}); }),
                  "imshow_rgb: a bad origin throws");
            check(throws([&] { ax->imshow_rgb(kRgb, 2, 2, 3, {.xrange = Range{1.0, 1.0}}); }),
                  "imshow_rgb: a degenerate range throws");
            check(ax->heatmap_count() == 3, "imshow_rgb: and a refused call adds nothing");

            auto log_fig = Figure::create();
            auto logged = log_fig->add_subplot(1, 1, 1);
            logged->set_yscale(Scale::Log);
            check(throws([&] { logged->imshow_rgb(kRgb, 2, 2, 3); }), "imshow_rgb: needs linear axes, as heatmap()");

            ax->imshow_rgb(kRgb, 2, 2, 3);
            const ObjectId id = ax->last_object();
            check(ax->remove(id) && ax->heatmap_count() == 3, "imshow_rgb: removed by its id");
        }

        // ---- Plane2D.
        {
            auto fig = Figure::create({.width = 400, .height = 400});
            auto ax3 = fig->add_subplot3d(1, 1, 1);
            auto pl = ax3->plane(PlaneOrientation::XY, 0.5);
            pl->imshow_rgb(kBytes, 2, 2, 3);
            check(pl->heatmap_count() == 1 && pl->heatmap_data(0).channels == 3, "plane: imshow_rgb is a heatmap there too");
            check(blob(fig->render_rgba(), 255, 0, 0).n > 100, "plane: and draws in its own colours");
        }

        // ---- A perspective plane's cells: a NaN one is left out.
        {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Perspective;
            s->camera.fov = 60.0;
            s->planes.push_back(make_plane(PlaneOrientation::XY, 0.5, {kNaN, 0.5f, 0.5f, 0.5f}, 2, 2,
                                           {0.05, 0.95}, {0.05, 0.95}));
            const FigureLayout lay = compute_figure_layout(fs, 400, 400);
            const std::vector<PlanePlanItem> plan = plan_planes3d(lay.cells[0].box3d->proj, s->planes);
            check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Polys && plan[0].polys.size() == 3,
                  "plane SVG: a perspective plane leaves its NaN cell out");
        }
    }
} // namespace lt
