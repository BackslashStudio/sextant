// Marker outlines: edgecolor / edge_alpha / edge_linewidth on
// scatter, scatter_z and scatter3d; a hollow marker (alpha 0) with only its
// outline; Cross and Plus as filled shapes. GPU pixels, SVG, legend keys, and
// the shared marker_shape(). Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "renderer/marker_shape.h"
#include "renderer/scatter3d.h"

namespace lt {
    using namespace sextant;

    namespace {
        // Pixels within `tol` of an opaque RGB.
        std::size_t count_near(const RgbaImage& img, int r, int g, int b, int tol) {
            std::size_t n = 0;
            for (std::size_t i = 0; i + 3 < img.pixels.size(); i += 4)
                if (std::abs(img.pixels[i] - r) <= tol && std::abs(img.pixels[i + 1] - g) <= tol &&
                    std::abs(img.pixels[i + 2] - b) <= tol)
                    ++n;
            return n;
        }

        std::size_t count_of(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        const std::vector<double> kX{5.0}, kY{5.0}, kZ{0.5};

        // One 40 px marker in the middle of a 400x300 figure.
        std::shared_ptr<Figure> one(const ScatterOptions& o, bool legend = false) {
            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ax->scatter(kX, kY, o);
            if (legend) ax->legend();
            return fig;
        }

        ScatterOptions base() {
            ScatterOptions o;
            o.size = 40.0f;
            o.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            o.alpha = 1.0f;
            return o;
        }

    } // namespace

    void test_edge_markers() {
        std::printf("\n[markers: outlines, hollow markers, Cross and Plus]\n");

        // --- 2D GPU ---------------------------------------------------------
        // A filled disc of diameter 40: about pi * 20^2 pixels.
        const std::size_t filled = count_near(one(base())->render_rgba(), 0, 0, 255, 8);
        check(filled > 1100 && filled < 1400, "an unoutlined marker is the same filled disc");

        // Hollow with a 3 px outline in the fill's own color: the ring only.
        {
            ScatterOptions o = base();
            o.alpha = 0.0f;
            o.edge_linewidth = 3.0f;
            const std::size_t ring = count_near(one(o)->render_rgba(), 0, 0, 255, 60);
            check(ring > 220 && ring < 480, "alpha 0 + edge_linewidth leaves the ring (in the fill color)");
            check(ring < filled / 2, "...and the middle is empty");
        }

        // Outline in its own color over the fill, drawn inside the boundary.
        {
            ScatterOptions o = base();
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_linewidth = 4.0f;
            const RgbaImage img = one(o)->render_rgba();
            const std::size_t red = count_near(img, 255, 0, 0, 40), blue = count_near(img, 0, 0, 255, 8);
            check(red > 300 && red < 600, "edgecolor: a red ring, 4 px wide");
            check(blue > 500 && blue < 900, "the fill is what is left inside it");
            check(red + blue < filled + 100, "the outline lies inside the marker's outer boundary");
        }

        // edge_alpha dims the outline independently of the fill's alpha.
        {
            ScatterOptions o = base();
            o.alpha = 0.0f;
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_alpha = 0.5f;
            o.edge_linewidth = 4.0f;
            const std::size_t half = count_near(one(o)->render_rgba(), 255, 128, 128, 25);
            check(half > 250, "edge_alpha 0.5 over white is a pink ring");
        }

        // Every shape is a filled region: hollow, each leaves ink but less than
        // its filled self.
        for (MarkerStyle m: {MarkerStyle::Circle, MarkerStyle::Square, MarkerStyle::Triangle,
                             MarkerStyle::Cross, MarkerStyle::Plus, MarkerStyle::Diamond}) {
            ScatterOptions f = base();
            f.marker = m;
            ScatterOptions h = f;
            h.alpha = 0.0f;
            h.edge_linewidth = 2.0f;
            const std::size_t full = count_near(one(f)->render_rgba(), 0, 0, 255, 60);
            const std::size_t ring = count_near(one(h)->render_rgba(), 0, 0, 255, 60);
            check(full > 100 && ring > 40 && ring < full,
                  "marker " + std::to_string(static_cast<int>(m)) + ": hollow outline < filled shape");
        }

        // The look of it, for the eye: every shape filled, outlined, and hollow,
        // on the GPU (marker_edges.png) and in the SVG (marker_edges.svg).
        {
            FigureOptions fo;
            fo.width = 640;
            fo.height = 300;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 7).set_ylim(0, 4);
            const MarkerStyle shapes[6] = {MarkerStyle::Circle, MarkerStyle::Square, MarkerStyle::Triangle,
                                           MarkerStyle::Cross, MarkerStyle::Plus, MarkerStyle::Diamond};
            for (int i = 0; i < 6; ++i) {
                const std::vector<double> x{i + 1.0}, y0{3.0}, y1{2.0}, y2{1.0};
                ScatterOptions f = base();
                f.marker = shapes[i];
                f.size = 36.0f;
                ax->scatter(x, y0, f);                     // filled
                f.edgecolor = Color{1.0f, 0.5f, 0.0f, 1.0f};
                f.edge_linewidth = 4.0f;
                ax->scatter(x, y1, f);                     // filled, orange outline
                f.alpha = 0.0f;
                f.edgecolor = std::nullopt;
                ax->scatter(x, y2, f);                     // hollow, own-color outline
            }
            fig->savefig("marker_edges.png");
            fig->savefig("marker_edges.svg");
        }

        // scatter_z: the outline defaults to each point's own color; an own one wins.
        {
            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ScatterZOptions z;
            z.size = 40.0f;
            z.alpha = 0.0f;
            z.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            z.edge_linewidth = 3.0f;
            ax->scatter_z(kX, kY, kZ, z);
            const std::size_t ring = count_near(fig->render_rgba(), 255, 0, 0, 60);
            check(ring > 220 && ring < 480, "scatter_z: hollow with a red outline");
            check(fig->render_svg().svg.find("stroke=\"rgb(255,0,0)\"") != std::string::npos,
                  "scatter_z: the SVG outlines it too");
        }

        // --- SVG -------------------------------------------------------------
        {
            ScatterOptions o = base();
            o.alpha = 0.0f;
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_alpha = 0.5f;
            o.edge_linewidth = 3.0f;
            const std::string svg = one(o)->render_svg().svg;
            check(svg.find("fill-opacity=\"0\"") != std::string::npos &&
                      svg.find("stroke=\"rgb(255,0,0)\"") != std::string::npos &&
                      svg.find("stroke-opacity=\"0.5\"") != std::string::npos &&
                      svg.find("stroke-width=\"3\"") != std::string::npos,
                  "SVG: hollow fill, red half-alpha outline, 3 px");
            // Drawn inside the boundary: radius 20 less half the stroke.
            check(svg.find("r=\"18.5\"") != std::string::npos, "SVG: the outline is inset by half its width");
            check(one(base())->render_svg().svg.find("stroke=\"rgb(0,0,255)\"") == std::string::npos,
                  "SVG: no outline without edge_linewidth");
        }

        // A legend key follows: the series' outline, hollow when alpha is 0.
        {
            ScatterOptions o = base();
            o.name = "pts";
            o.alpha = 0.0f;
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_linewidth = 3.0f;
            const std::string svg = one(o, true)->render_svg().svg;
            check(count_of(svg, "stroke=\"rgb(255,0,0)\"") >= 2, "legend key: outlined like the marker");
        }

        // --- 3D --------------------------------------------------------------
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot3d(1, 1, 1);
            const std::vector<double> x{0.0, 1.0, 2.0}, y{0.0, 1.0, 0.5}, z{0.0, 1.0, 2.0};
            Scatter3DOptions o;
            o.size = 30.0f;
            o.alpha = 0.0f;
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_linewidth = 3.0f;
            ax->scatter3d(x, y, z, o);
            const RgbaImage img = fig->render_rgba();
            check(count_near(img, 255, 0, 0, 60) > 150 && count_near(img, 0, 0, 255, 60) == 0,
                  "scatter3d: hollow, red outline only");
            const std::string svg = fig->render_svg().svg;
            check(count_of(svg, "stroke=\"rgb(255,0,0)\"") >= 3, "scatter3d: SVG outlines each marker");
        }

        // An outline below full alpha forces the composited path, like the fill.
        {
            Scatter3DPlot p;
            p.opts.edge_linewidth = 2.0f;
            check(!scatter3d_translucent(p), "scatter3d: an opaque outline keeps the cheap path");
            p.opts.edge_alpha = 0.4f;
            check(scatter3d_translucent(p), "scatter3d: a translucent outline takes the composited path");
            p.opts.edge_linewidth = 0.0f;
            check(!scatter3d_translucent(p), "scatter3d: no outline, no effect");
        }

        // --- marker_shape() --------------------------------------------------
        {
            const MarkerShape plus = marker_shape(MarkerStyle::Plus, 0, 0, 10.0f);
            const MarkerShape cross = marker_shape(MarkerStyle::Cross, 0, 0, 10.0f);
            check(plus.form == MarkerShape::Form::Polygon && plus.count == 12 &&
                      cross.form == MarkerShape::Form::Polygon && cross.count == 12,
                  "Plus and Cross are twelve-corner filled shapes");
            auto extent = [](const MarkerShape& s) {
                float e = 0.0f;
                for (int i = 0; i < s.count; ++i)
                    e = std::max({e, std::fabs(s.pts[i][0]), std::fabs(s.pts[i][1])});
                return e;
            };
            check(std::fabs(extent(plus) - 10.0f) < 1e-4f, "Plus reaches the marker's half-size");
            check(std::fabs(extent(marker_shape(MarkerStyle::Plus, 0, 0, 10.0f, 2.0f)) - 8.0f) < 1e-4f,
                  "an inset pulls every edge in by that much");
            check(marker_shape(MarkerStyle::Circle, 0, 0, 10.0f, 2.5f).radius == 7.5f, "Circle: radius less the inset");
            const MarkerShape dia = marker_shape(MarkerStyle::Diamond, 0, 0, 10.0f, 1.0f);
            check(std::fabs(dia.pts[0][1] + (10.0f - 1.4142136f)) < 1e-4f, "Diamond: the inset follows its slanted edges");
        }
    }
} // namespace lt
