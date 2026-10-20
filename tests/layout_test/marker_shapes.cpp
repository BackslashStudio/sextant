// The marker shapes: the side triangles, Star, Pentagon and
// Hexagon (filled, outlined as the others), and the line markers ThinCross,
// ThinPlus, VLine and HLine (a stroke in the fill's color, `edge_linewidth`
// thick). marker_shape() geometry, GPU pixels against the SVG polygon (area and
// which way the shape points), legend keys, and the 3D translucency rule. Part
// of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "renderer/marker_shape.h"
#include "renderer/scatter3d.h"
#include <sstream>

namespace lt {
    using namespace sextant;

    namespace {
        const MarkerStyle kFilled[] = {MarkerStyle::TriangleDown, MarkerStyle::TriangleLeft,
                                       MarkerStyle::TriangleRight, MarkerStyle::Star,
                                       MarkerStyle::Pentagon, MarkerStyle::Hexagon};
        const MarkerStyle kLines[] = {MarkerStyle::ThinCross, MarkerStyle::ThinPlus,
                                      MarkerStyle::VLine, MarkerStyle::HLine};

        std::string name_of(MarkerStyle m) { return "marker " + std::to_string(static_cast<int>(m)); }

        // Shoelace area and centroid of a polygon marker.
        void polygon_area(const MarkerShape& s, double& area, double& gx, double& gy) {
            double a = 0, x = 0, y = 0;
            for (int i = 0; i < s.count; ++i) {
                const int j = (i + 1) % s.count;
                const double c = s.pts[i][0] * s.pts[j][1] - s.pts[j][0] * s.pts[i][1];
                a += c;
                x += (s.pts[i][0] + s.pts[j][0]) * c;
                y += (s.pts[i][1] + s.pts[j][1]) * c;
            }
            area = std::fabs(a) * 0.5;
            gx = a != 0 ? x / (3.0 * a) : 0;
            gy = a != 0 ? y / (3.0 * a) : 0;
        }

        // Distance from (cx, cy) to the line through each polygon edge, smallest.
        float nearest_edge_line(const MarkerShape& s, float cx, float cy) {
            float d = 1e30f;
            for (int i = 0; i < s.count; ++i) {
                const int j = (i + 1) % s.count;
                const float ex = s.pts[j][0] - s.pts[i][0], ey = s.pts[j][1] - s.pts[i][1];
                const float c = (ex * (cy - s.pts[i][1]) - ey * (cx - s.pts[i][0])) / std::hypot(ex, ey);
                d = std::min(d, std::fabs(c));
            }
            return d;
        }

        // Ink near an opaque RGB: count, and centroid relative to the ink's own
        // bounding-box centre (so the marker's absolute position drops out).
        struct Ink {
            std::size_t n = 0;
            double dx = 0, dy = 0;
            int w = 0, h = 0;
        };

        Ink ink(const RgbaImage& img, int r, int g, int b, int tol) {
            Ink k;
            double sx = 0, sy = 0;
            int x0 = img.width, x1 = -1, y0 = img.height, y1 = -1;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    const std::uint8_t* p = &img.pixels[(static_cast<std::size_t>(y) * img.width + x) * 4];
                    if (std::abs(p[0] - r) > tol || std::abs(p[1] - g) > tol || std::abs(p[2] - b) > tol)
                        continue;
                    ++k.n;
                    sx += x;
                    sy += y;
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
            if (k.n) {
                k.dx = sx / k.n - 0.5 * (x0 + x1);
                k.dy = sy / k.n - 0.5 * (y0 + y1);
                k.w = x1 - x0 + 1;
                k.h = y1 - y0 + 1;
            }
            return k;
        }

        // How much pure blue the image holds, in pixels: each pixel counts by how
        // far its blue exceeds its red and green (white, grey and black count 0).
        double blue_coverage(const RgbaImage& img) {
            double s = 0.0;
            for (std::size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
                const int c = img.pixels[i + 2] - std::max(img.pixels[i], img.pixels[i + 1]);
                if (c > 0) s += c / 255.0;
            }
            return s;
        }

        // Width + height of the box around every pixel at least a quarter blue
        // (by blue_coverage()'s measure); 0 when there is none.
        int blue_extent(const RgbaImage& img) {
            int x0 = img.width, x1 = -1, y0 = img.height, y1 = -1;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    const std::uint8_t* p = &img.pixels[(static_cast<std::size_t>(y) * img.width + x) * 4];
                    if (p[2] - std::max(p[0], p[1]) < 64) continue;
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
            return x1 < 0 ? 0 : (x1 - x0 + 1) + (y1 - y0 + 1);
        }

        const std::vector<double> kX{5.0}, kY{5.0}, kZ{0.5};

        // One marker in the middle of a 400x300 figure with no frame clutter
        // near it.
        std::shared_ptr<Figure> one(const ScatterOptions& o, bool legend = false) {
            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            fo.supersample = 1;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ax->scatter(kX, kY, o);
            if (legend) ax->legend();
            return fig;
        }

        ScatterOptions base(MarkerStyle m, float size = 40.0f) {
            ScatterOptions o;
            o.size = size;
            o.marker = m;
            o.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            o.alpha = 1.0f;
            return o;
        }

        // The first blue <polygon>'s coordinates, x y x y ...
        std::vector<float> first_polygon(const std::string& svg) {
            std::vector<float> out;
            for (std::size_t at = svg.find("<polygon points=\""); at != std::string::npos;
                 at = svg.find("<polygon points=\"", at + 1)) {
                const std::size_t b = at + 17, e = svg.find('"', b);
                const std::size_t close = svg.find("/>", e);
                if (svg.substr(e, close - e).find("fill=\"rgb(0,0,255)\"") == std::string::npos) continue;
                std::string pts = svg.substr(b, e - b);
                for (char& c: pts) if (c == ',') c = ' ';
                std::istringstream in(pts);
                for (float f; in >> f;) out.push_back(f);
                break;
            }
            return out;
        }

        std::size_t count_of(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }
    } // namespace

    void test_marker_shapes() {
        std::printf("\n[markers: side triangles, star, pentagon, hexagon, line markers]\n");

        // --- marker_shape() --------------------------------------------------
        {
            const float r = 10.0f;
            const MarkerShape down = marker_shape(MarkerStyle::TriangleDown, 0, 0, r);
            const MarkerShape left = marker_shape(MarkerStyle::TriangleLeft, 0, 0, r);
            const MarkerShape right = marker_shape(MarkerStyle::TriangleRight, 0, 0, r);
            auto has = [](const MarkerShape& s, float x, float y) {
                for (int i = 0; i < s.count; ++i)
                    if (std::fabs(s.pts[i][0] - x) < 1e-4f && std::fabs(s.pts[i][1] - y) < 1e-4f) return true;
                return false;
            };
            check(down.count == 3 && has(down, 0, r) && has(down, r, -r) && has(down, -r, -r),
                  "TriangleDown: apex at the bottom (y grows down), base on top");
            check(left.count == 3 && has(left, -r, 0) && has(left, r, r) && has(left, r, -r),
                  "TriangleLeft: apex at the left");
            check(right.count == 3 && has(right, r, 0) && has(right, -r, r) && has(right, -r, -r),
                  "TriangleRight: apex at the right");

            const MarkerShape star = marker_shape(MarkerStyle::Star, 0, 0, r);
            const MarkerShape pent = marker_shape(MarkerStyle::Pentagon, 0, 0, r);
            const MarkerShape hex = marker_shape(MarkerStyle::Hexagon, 0, 0, r);
            check(star.count == 10 && pent.count == 5 && hex.count == 6,
                  "Star has ten corners, Pentagon five, Hexagon six");
            check(has(star, 0, -r) && has(pent, 0, -r) && has(hex, 0, -r),
                  "Star, Pentagon and Hexagon point straight up");
            float far = 0.0f;
            for (const MarkerShape* s: {&star, &pent, &hex})
                for (int i = 0; i < s->count; ++i) far = std::max(far, std::hypot(s->pts[i][0], s->pts[i][1]));
            check(std::fabs(far - r) < 1e-4f, "...each within the marker's radius, touching it");

            // An inset moves every edge in by that much (mitred corners).
            for (const MarkerStyle m: {MarkerStyle::Pentagon, MarkerStyle::Hexagon, MarkerStyle::Star,
                                       MarkerStyle::TriangleLeft}) {
                const MarkerShape a = marker_shape(m, 0, 0, r), b = marker_shape(m, 0, 0, r, 1.5f);
                check(a.count == b.count &&
                          std::fabs(nearest_edge_line(a, 0, 0) - nearest_edge_line(b, 0, 0) - 1.5f) < 1e-3f,
                      name_of(m) + ": an inset of 1.5 moves its edges 1.5 inward");
            }

            // Line markers: `line_w` thick whatever the size, reaching the radius.
            const MarkerShape v = marker_shape(MarkerStyle::VLine, 0, 0, r, 3.0f, 2.0f);
            check(v.count == 4 && std::fabs(v.pts[0][0] + 1.0f) < 1e-4f && std::fabs(v.pts[0][1] + r) < 1e-4f,
                  "VLine: 2 px wide, full height, the inset ignored");
            const MarkerShape h = marker_shape(MarkerStyle::HLine, 0, 0, r);
            check(h.count == 4 && std::fabs(h.pts[2][0] - r) < 1e-4f &&
                      std::fabs(h.pts[2][1] - kLineMarkerWidth * 0.5f) < 1e-4f,
                  "HLine: full width, 1.5 px tall by default");
            const MarkerShape tp = marker_shape(MarkerStyle::ThinPlus, 0, 0, r, 0.0f, 2.0f);
            check(tp.count == 12 && std::fabs(tp.pts[0][0] + 1.0f) < 1e-4f && std::fabs(tp.pts[0][1] + r) < 1e-4f,
                  "ThinPlus: two 2 px bars to the radius");
            check(is_line_marker(MarkerStyle::ThinCross) && !is_line_marker(MarkerStyle::Cross) &&
                      line_marker_width(0.0f) == kLineMarkerWidth && line_marker_width(3.0f) == 3.0f,
                  "is_line_marker() / line_marker_width()");
        }

        // --- GPU against the polygon ----------------------------------------
        // Each filled shape at 40 px: the pixel count is the polygon's area, and
        // the ink leans the way the polygon's centroid does (a mirrored or turned
        // shader shape would lean the other way).
        for (const MarkerStyle m: kFilled) {
            const Ink k = ink(one(base(m))->render_rgba(), 0, 0, 255, 8);
            double area = 0, gx = 0, gy = 0;
            const MarkerShape s = marker_shape(m, 0, 0, 20.0f);
            polygon_area(s, area, gx, gy);
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
            for (int i = 0; i < s.count; ++i) {
                x0 = std::min(x0, s.pts[i][0]);
                x1 = std::max(x1, s.pts[i][0]);
                y0 = std::min(y0, s.pts[i][1]);
                y1 = std::max(y1, s.pts[i][1]);
            }
            const double lean_x = gx - 0.5 * (x0 + x1), lean_y = gy - 0.5 * (y0 + y1);
            check(k.n > area * 0.92 && k.n < area * 1.08,
                  name_of(m) + ": GPU pixels " + std::to_string(k.n) + " ~ polygon area " +
                      std::to_string(static_cast<int>(area)));
            check(std::fabs(k.dx - lean_x) < 1.0 && std::fabs(k.dy - lean_y) < 1.0,
                  name_of(m) + ": the GPU shape points the same way as the polygon");
        }

        // Filled shapes take outlines as before: hollow leaves a ring of ink.
        for (const MarkerStyle m: kFilled) {
            ScatterOptions h = base(m);
            h.alpha = 0.0f;
            h.edge_linewidth = 2.0f;
            const std::size_t full = ink(one(base(m))->render_rgba(), 0, 0, 255, 60).n;
            const std::size_t ring = ink(one(h)->render_rgba(), 0, 0, 255, 60).n;
            check(ring > 40 && ring < full, name_of(m) + ": hollow outline < filled shape");
        }

        // Line markers: the stroke's thickness stays put as the marker grows, so
        // the ink grows with the length alone. Checked as the two things apart:
        // the extent (bounding box w + h) grows 3x, and the thickness (summed
        // coverage over that extent) stays within a pixel. The ink ratio alone
        // is not stable: rendered aliased (llvmpipe, no MSAA) a 1.5 px stroke
        // whose edge falls on a pixel centre keeps 1 row or 2 by float rounding
        // (1 row at 20 px and 2 at 60, reading 6x), where a filled shape would
        // read 9x on both counts.
        for (const MarkerStyle m: kLines) {
            const RgbaImage si = one(base(m, 20.0f))->render_rgba();
            const RgbaImage bi = one(base(m, 60.0f))->render_rgba();
            const double sext = blue_extent(si), bext = blue_extent(bi);
            const double sthick = sext > 0 ? blue_coverage(si) / sext : 0.0;
            const double bthick = bext > 0 ? blue_coverage(bi) / bext : 0.0;
            check(sext > 10 && bext > sext * 2.6 && bext < sext * 3.4 && sthick > 0.4 &&
                      std::fabs(bthick - sthick) < 1.2,
                  name_of(m) + ": 3x the size is 3x the length at the same thickness (extent " +
                      std::to_string(sext) + " -> " + std::to_string(bext) + ", thickness " +
                      std::to_string(sthick) + " -> " + std::to_string(bthick) + "), not 9x the ink");
            ScatterOptions w = base(m, 60.0f);
            w.edge_linewidth = 6.0f;
            const double big = blue_coverage(bi), wide = blue_coverage(one(w)->render_rgba());
            check(wide > big * 2.5, name_of(m) + ": edge_linewidth 6 is its stroke width (" +
                                        std::to_string(big) + " -> " + std::to_string(wide) + ")");
        }
        {
            const Ink v = ink(one(base(MarkerStyle::VLine, 40.0f))->render_rgba(), 0, 0, 255, 60);
            const Ink h = ink(one(base(MarkerStyle::HLine, 40.0f))->render_rgba(), 0, 0, 255, 60);
            check(v.h >= 38 && v.w <= 3 && h.w >= 38 && h.h <= 3, "VLine is upright, HLine lies flat");

            // No outline: an edge color never shows; the stroke is the fill's.
            ScatterOptions o = base(MarkerStyle::ThinPlus, 40.0f);
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_linewidth = 3.0f;
            const RgbaImage img = one(o)->render_rgba();
            check(ink(img, 255, 0, 0, 60).n == 0 && ink(img, 0, 0, 255, 60).n > 150,
                  "a line marker ignores edgecolor: blue strokes, no red");
            o.alpha = 0.0f;
            check(ink(one(o)->render_rgba(), 0, 0, 255, 120).n == 0, "alpha 0 hides a line marker");
        }

        // scatter_z: each point's own color.
        {
            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ScatterZOptions z;
            z.size = 40.0f;
            z.alpha = 1.0f;
            z.marker = MarkerStyle::ThinCross;
            z.edge_linewidth = 4.0f;
            z.vmin = 0.0f;
            z.vmax = 1.0f;
            z.cmap = Colormap::Viridis;
            ax->scatter_z(kX, kY, std::vector<double>{1.0}, z);  // viridis top: (253, 231, 37)
            check(ink(fig->render_rgba(), 253, 231, 37, 20).n > 60, "scatter_z: a line marker in its point's color");
        }

        // --- SVG -------------------------------------------------------------
        {
            ScatterOptions o = base(MarkerStyle::VLine, 40.0f);
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_linewidth = 4.0f;
            const std::string svg = one(o)->render_svg().svg;
            const std::vector<float> v = first_polygon(svg);
            check(svg.find("stroke=\"rgb(255,0,0)\"") == std::string::npos && v.size() == 8 &&
                      std::fabs(v[2] - v[0] - 4.0f) < 1e-3f && std::fabs(v[5] - v[3] - 40.0f) < 1e-3f,
                  "SVG: a 4 x 40 px VLine polygon in the fill color, no outline");
            const std::vector<float> s = first_polygon(one(base(MarkerStyle::Star))->render_svg().svg);
            float top = 1e9f;
            for (std::size_t i = 1; i < s.size(); i += 2) top = std::min(top, s[i]);
            check(s.size() == 20 && s[1] == top, "SVG: a ten-corner Star from its top point");
        }

        // --- Legend keys ------------------------------------------------------
        {
            // A line marker's key is its stroke, whatever the outline settings.
            ScatterOptions o = base(MarkerStyle::ThinPlus, 40.0f);
            o.name = "pts";
            o.edgecolor = Color{1.0f, 0.0f, 0.0f, 1.0f};
            o.edge_linewidth = 3.0f;
            const std::string svg = one(o, true)->render_svg().svg;
            check(count_of(svg, "stroke=\"rgb(255,0,0)\"") == 0 && count_of(svg, "fill=\"rgb(0,0,255)\"") >= 2,
                  "legend: a line marker's key is a blue stroke, no outline");

            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ScatterZOptions z;
            z.marker = MarkerStyle::HLine;
            z.name = "z";
            ax->scatter_z(kX, kY, kZ, z);
            ax->legend();
            const std::string zs = fig->render_svg().svg;
            check(zs.find("fill=\"rgb(0,0,0)\" fill-opacity=\"1\"/>") != std::string::npos &&
                      zs.find("fill=\"rgb(255,255,255)\" fill-opacity=\"1\" stroke") == std::string::npos,
                  "legend: scatter_z's line-marker key is black, not the white fill key");
        }

        // --- 3D --------------------------------------------------------------
        {
            Scatter3DPlot p;
            p.opts.marker = MarkerStyle::ThinCross;
            p.opts.edge_linewidth = 2.0f;
            p.opts.edge_alpha = 0.4f;
            check(!scatter3d_translucent(p), "scatter3d: a line marker's edge alpha never forces compositing");
            p.opts.marker = MarkerStyle::Star;
            check(scatter3d_translucent(p), "scatter3d: a filled one's still does");

            auto fig = Figure::create();
            auto ax = fig->add_subplot3d(1, 1, 1);
            const std::vector<double> x{0.0, 1.0, 2.0}, y{0.0, 1.0, 0.5}, z{0.0, 1.0, 2.0};
            Scatter3DOptions o;
            o.size = 30.0f;
            o.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            o.marker = MarkerStyle::Hexagon;
            ax->scatter3d(x, y, z, o);
            check(ink(fig->render_rgba(), 0, 0, 255, 40).n > 3 * 400, "scatter3d: hexagons on the GPU");
            check(count_of(fig->render_svg().svg, "<polygon") >= 3, "scatter3d: hexagon polygons in the SVG");
        }

        // The look of it, for the eye: every shape filled, outlined, and hollow
        // (line markers: default, 3 px, and in an edge color they ignore).
        {
            FigureOptions fo;
            fo.width = 900;
            fo.height = 300;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 11).set_ylim(0, 4);
            std::vector<MarkerStyle> all(std::begin(kFilled), std::end(kFilled));
            all.insert(all.end(), std::begin(kLines), std::end(kLines));
            for (std::size_t i = 0; i < all.size(); ++i) {
                const std::vector<double> x{i + 1.0}, y0{3.0}, y1{2.0}, y2{1.0};
                ScatterOptions f = base(all[i], 36.0f);
                ax->scatter(x, y0, f);
                f.edgecolor = Color{1.0f, 0.5f, 0.0f, 1.0f};
                f.edge_linewidth = 3.0f;
                ax->scatter(x, y1, f);
                f.alpha = is_line_marker(all[i]) ? 1.0f : 0.0f;
                f.edgecolor = std::nullopt;
                ax->scatter(x, y2, f);
            }
            fig->savefig("marker_shapes.png");
            fig->savefig("marker_shapes.svg");
        }
    }
} // namespace lt
