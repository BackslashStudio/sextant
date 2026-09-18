// 3D error bars (v1.0 step 17): the ErrorBar3D parameter on scatter3d/line3d,
// the pieces a whisker, its caps and its block reduce to, and what the two
// outputs and the hover draw from them.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "renderer/error_bar3d.h"
#include <type_traits>

namespace lt {

using namespace sextant;

namespace {

std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Every element of `tag` ("<line " or "<polygon ") carrying `attr`, and
// `also` when that is given.
std::size_t count_elements(const std::string& svg, const std::string& tag,
                           const std::string& attr, const std::string& also = {}) {
    std::size_t n = 0;
    for (std::size_t p = svg.find(tag); p != std::string::npos; p = svg.find(tag, p + 1)) {
        const std::size_t end = svg.find("/>", p);
        const std::string el = svg.substr(p, end - p);
        if (el.find(attr) != std::string::npos &&
            (also.empty() || el.find(also) != std::string::npos)) ++n;
    }
    return n;
}

// One point at the box centre over 0..10 limits, with no marker, so whatever
// is drawn is the error bar.
Scatter3DPlot one_point(double at = 5.0) {
    Scatter3DPlot s;
    s.x = std::vector<double>{ at };
    s.y = std::vector<double>{ 5.0 };
    s.z = std::vector<double>{ 5.0 };
    s.opts.marker = MarkerStyle::None;
    return s;
}

Transform3D ten_box() {
    Transform3D tf;
    tf.xmin = 0.0; tf.xmax = 10.0;
    tf.ymin = 0.0; tf.ymax = 10.0;
    tf.zmin = 0.0; tf.zmax = 10.0;
    return tf;
}

Camera3D oblique(Projection mode) {
    Camera3D cam;
    cam.azimuth = -55.0; cam.elevation = 24.0;
    cam.projection = mode;
    return cam;
}

const PlotRect kFrame{ 20.0f, 15.0f, 400.0f, 320.0f };

int count_faces(const std::vector<ErrorBar3DPiece>& v) {
    return static_cast<int>(std::count_if(v.begin(), v.end(),
                                          [](const ErrorBar3DPiece& p) { return p.face; }));
}
int count_strokes(const std::vector<ErrorBar3DPiece>& v) {
    return static_cast<int>(v.size()) - count_faces(v);
}

// The block's extent on box axis `a`, read off its pieces.
double extent(const std::vector<ErrorBar3DPiece>& v, int a) {
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    for (const ErrorBar3DPiece& p : v) {
        if (!p.face) continue;
        for (const Vec3& q : p.p) {
            const double c = a == 0 ? q.x : a == 1 ? q.y : q.z;
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
    }
    return hi - lo;
}

} // namespace

// -------------------------------------------------------------------------
// The public API: the length rule, the style struct, and the public path
// -------------------------------------------------------------------------
void test_errorbar3d_api() {
    std::printf("\n[3D error bars: the ErrorBar3D parameter and its style]\n");

    check(!std::is_same_v<ErrorBar3DOptions, ErrorBarOptions>,
          "api: ErrorBar3DOptions is its own struct again -- a block's edges need an "
          "opacity 2D has no use for");
    const ErrorBar3DOptions st;
    check(st.box_alpha == 0.25f && st.edge_alpha == 0.5f,
          "api: a block is translucent by default -- faces at 0.25, edges at 0.5");
    check(!ErrorBar3D{}.any(), "api: an empty ErrorBar3D sets nothing");

    const std::vector<double> x{ 2.0, 5.0, 8.0 }, y{ 2.0, 6.0, 3.0 }, z{ 3.0, 5.0, 7.0 };
    const std::vector<double> e3{ 0.5, 0.25, 1.0 }, e2{ 0.5, 0.25 };

    {
        auto fig = Figure::create({ .width = 300, .height = 240 });
        auto ax = fig->add_subplot3d(1, 1, 1);
        std::string msg;
        try { ax->scatter3d(x, y, z, { .y_box_lo = e2 }); }
        catch (const std::invalid_argument& ex) { msg = ex.what(); }
        check(msg.find("err.y_box_lo has 2 entries, need one per point (3)") != std::string::npos,
              "api: a wrong-length span names itself and both counts, in 2D's words");
    }

    // An empty ErrorBar3D draws exactly what no ErrorBar3D draws.
    {
        auto a = Figure::create({ .width = 320, .height = 260 });
        a->add_subplot3d(1, 1, 1)->scatter3d(x, y, z);
        a->savefig("eb3d_none.svg");
        auto b = Figure::create({ .width = 320, .height = 260 });
        b->add_subplot3d(1, 1, 1)->scatter3d(x, y, z, ErrorBar3D{});
        b->savefig("eb3d_empty.svg");
        check(read_text("eb3d_none.svg") == read_text("eb3d_empty.svg"),
              "api: an empty ErrorBar3D produces the same SVG as none at all");
    }

    // Through the public path to the file: three flat-capped z whiskers are
    // three stems and six cap bars, in the colour the style asked for.
    {
        auto fig = Figure::create({ .width = 360, .height = 300 });
        Scatter3DOptions o;
        o.errorbar.color = Color{ 0.0f, 0.6f, 0.0f, 1.0f };
        fig->add_subplot3d(1, 1, 1)->scatter3d(x, y, z, { .z_cap_lo = e3 }, o);
        fig->savefig("eb3d_public.svg");
        const std::string svg = read_text("eb3d_public.svg");
        check(count_elements(svg, "<line ", "stroke=\"rgb(0,153,0)\"") == 9,
              "api: scatter3d draws a stem and two caps per point through the public API");

        auto fig2 = Figure::create({ .width = 360, .height = 300 });
        Line3DOptions lo;
        lo.errorbar.color = Color{ 0.0f, 0.6f, 0.0f, 1.0f };
        fig2->add_subplot3d(1, 1, 1)->line3d(x, y, z, { .x_cap_hi = e3 }, lo);
        fig2->savefig("eb3d_public_line.svg");
        check(count_elements(read_text("eb3d_public_line.svg"), "<line ",
                             "stroke=\"rgb(0,153,0)\"") == 9,
              "api: and so does line3d");
    }
}

// -------------------------------------------------------------------------
// The stored data: step 16's rules in three directions, and the limits
// -------------------------------------------------------------------------
void test_errorbar3d_data() {
    std::printf("\n[3D error bars: offsets, symmetry, masking and the limits]\n");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    ErrorBar3DData d;
    d.cap_lo[2] = std::vector<double>{ 0.5, -2.0, nan };
    d.box_hi[0] = std::vector<double>{ 1.0, 0.0, 3.0 };
    d.box_lo[0] = std::vector<double>{ 0.25, 0.0, 0.0 };

    check(d.has_cap(2) && !d.has_cap(0) && d.has_box(0) && !d.has_box(1) && d.any_box(),
          "data: which axes carry caps and boxes is which spans were given");
    const ErrOffsets c0 = d.cap(2, 0), c1 = d.cap(2, 1), c2 = d.cap(2, 2);
    check(c0.lo == 0.5 && c0.hi == 0.5, "data: one end given means symmetric");
    check(c1.lo == 2.0 && c1.hi == 2.0, "data: a negative offset is its magnitude");
    check(!c2.any(), "data: a non-finite entry reads as zero, masking that point's bar");
    const ErrOffsets b0 = d.box(0, 0);
    check(b0.lo == 0.25 && b0.hi == 1.0, "data: both ends given are read as given");

    // The limits reach the error bars' data extent, per axis.
    Scatter3DPlot s;
    s.x = std::vector<double>{ 1.0, 2.0, 3.0 };
    s.y = std::vector<double>{ 1.0, 2.0, 3.0 };
    s.z = std::vector<double>{ 1.0, 2.0, 3.0 };
    s.err = d;
    const DataBounds3D b = auto_scale3d({}, {}, {}, { s }, {}, {}, 0.0);
    // z: 1 +- 0.5, 2 +- 2 and 3 masked, so 0 .. 4.
    check(near_px(static_cast<float>(b.zmin), 0.0f) && near_px(static_cast<float>(b.zmax), 4.0f),
          "data: auto-scale grows z by the cap offsets, a masked entry contributing nothing");
    check(near_px(static_cast<float>(b.xmin), 0.75f) && near_px(static_cast<float>(b.xmax), 6.0f),
          "data: and x by the box offsets");
    check(b.ymin == 1.0 && b.ymax == 3.0, "data: an axis with no error data is its points");

    Line3DPlot l;
    l.x = s.x; l.y = s.y; l.z = s.z;
    l.err = d;
    const DataBounds3D lb = auto_scale3d({}, {}, {}, {}, { l }, {}, 0.0);
    check(lb.xmin == b.xmin && lb.xmax == b.xmax && lb.zmax == b.zmax,
          "data: a path's error bars grow the limits on the same terms");
}

// -------------------------------------------------------------------------
// The pieces: what a whisker, a cap and a block are, in box space
// -------------------------------------------------------------------------
void test_errorbar3d_geometry() {
    std::printf("\n[3D error bars: whiskers, caps and the block, as pieces]\n");

    const Transform3D tf = ten_box();
    const Projector3D proj(tf, oblique(Projection::Orthographic), kFrame, 0.1f);
    const double k = proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });

    auto build = [&](const Scatter3DPlot& s, const Projector3D& pj) {
        std::vector<ErrorBar3DPiece> v;
        errorbar3d_pieces(pj, s, 0, v);
        return v;
    };

    // ---- A flat-capped z whisker.
    {
        Scatter3DPlot s = one_point();
        s.err.cap_lo[2] = std::vector<double>{ 2.0 };
        s.opts.errorbar.linewidth = 3.0f;
        s.opts.errorbar.capsize = 20.0f;
        const auto v = build(s, proj);
        check(count_strokes(v) == 3 && count_faces(v) == 0,
              "geometry: a flat-capped whisker is a stem and two cap bars, and no block");
        if (v.size() == 3) {
            const Vec3 lo = tf.to_box(5.0, 5.0, 3.0), hi = tf.to_box(5.0, 5.0, 7.0);
            check(length(v[0].p[0] - lo) < 1e-9 && length(v[0].p[1] - hi) < 1e-9,
                  "geometry: the stem runs from the point minus its offset to plus it");
            const Vec3 bar = v[1].p[1] - v[1].p[0];
            check(std::fabs(length(bar) - 20.0 * k) < 1e-9,
                  "geometry: a cap bar is `capsize` pixels long at the box centre");
            check(std::fabs(dot(normalize(bar), v[1].facing)) < 1e-9 &&
                  std::fabs(normalize(bar).z) < 1e-9,
                  "geometry: and lies across the whisker in the plane facing the eye");
            check(std::fabs(v[0].half_width - 1.5 * k) < 1e-12,
                  "geometry: every stroke is `linewidth` pixels wide at the box centre");
            check(length(v[0].facing - v[2].facing) < 1e-12,
                  "geometry: a whisker and its caps share one facing -- rigid, not twisting");
            Vec3 q[4];
            check(errorbar3d_ribbon(v[0], q) &&
                  std::fabs(dot(normalize(q[1] - q[0]), v[0].facing)) < 1e-9,
                  "geometry: the ribbon is expanded across the view, so it faces the eye");
        }

        s.opts.errorbar.capstyle = CapStyle::Arrow;
        check(count_strokes(build(s, proj)) == 5,
              "geometry: an arrow cap is two chevron arms per end");

        s.opts.errorbar.capstyle = CapStyle::Flat;
        s.err.cap_hi[2] = std::vector<double>{ 0.0 };
        check(count_strokes(build(s, proj)) == 2,
              "geometry: a zero offset draws no cap on that side");

        s.opts.errorbar.linewidth = 0.0f;
        check(build(s, proj).empty(), "geometry: linewidth 0 draws no error bar at all");
    }

    // ---- The block.
    {
        Scatter3DPlot s = one_point();
        s.err.box_lo[1] = std::vector<double>{ 1.0 };
        s.err.box_hi[1] = std::vector<double>{ 3.0 };
        s.opts.errorbar.boxwidth = 30.0f;
        auto v = build(s, proj);
        check(count_faces(v) == 6 && count_strokes(v) == 12,
              "geometry: box data draws a block -- six faces and twelve edges");
        const double ybox = tf.to_box(5.0, 8.0, 5.0).y - tf.to_box(5.0, 4.0, 5.0).y;
        check(std::fabs(extent(v, 1) - ybox) < 1e-9,
              "geometry: along an axis with box data the block spans its offsets");
        check(std::fabs(extent(v, 0) - 30.0 * k) < 1e-9 && std::fabs(extent(v, 2) - 30.0 * k) < 1e-9,
              "geometry: along the axes without, it is `boxwidth` pixels at the box centre");

        s.err.box_lo[0] = std::vector<double>{ 2.0 };
        s.err.box_lo[2] = std::vector<double>{ 0.5 };
        const auto all3 = build(s, proj);
        s.opts.errorbar.boxwidth = 90.0f;
        const auto all3b = build(s, proj);
        check(std::fabs(extent(all3, 0) - extent(all3b, 0)) < 1e-12 &&
              std::fabs(extent(all3, 2) - extent(all3b, 2)) < 1e-12 &&
              std::fabs(extent(all3, 0) - (tf.to_box(7.0, 5, 5).x - tf.to_box(3.0, 5, 5).x)) < 1e-9,
              "geometry: with box data on all three axes, boxwidth plays no part");

        Scatter3DPlot flat = one_point();
        flat.err.box_lo[2] = std::vector<double>{ 0.0 };
        check(build(flat, proj).empty(),
              "geometry: box data of zero extent draws no block");

        s.opts.errorbar.box_alpha = 0.0f;
        v = build(s, proj);
        check(count_faces(v) == 0 && count_strokes(v) == 12,
              "geometry: box_alpha 0 drops the faces and keeps the edges");
        s.opts.errorbar.box_alpha = 0.25f;
        s.opts.errorbar.edge_alpha = 0.0f;
        v = build(s, proj);
        check(count_faces(v) == 6 && count_strokes(v) == 0,
              "geometry: edge_alpha 0 drops the edges and keeps the faces");
        s.opts.errorbar.edge_alpha = 0.5f;
        v = build(s, proj);
        bool alphas = !v.empty();
        for (const ErrorBar3DPiece& p : v)
            alphas = alphas && near_px(p.color.a, p.face ? 0.25f : 0.5f);
        check(alphas, "geometry: faces carry box_alpha and edges edge_alpha, over the colour's own");
    }

    // ---- Colour.
    {
        Scatter3DPlot s = one_point();
        s.opts.color = Color::Red;
        check(errorbar3d_color(s).r == Color::Red.r && errorbar3d_color(s).g == Color::Red.g,
              "colour: unset, a flat series' bars take its colour");
        s.colors = std::vector<double>{ 1.0 };
        const Color c = errorbar3d_color(s);
        check(c.r == 0.0f && c.g == 0.0f && c.b == 0.0f,
              "colour: and a colormapped series' bars are black");
        s.opts.errorbar.color = Color{ 0.0f, 0.6f, 0.0f, 1.0f };
        check(errorbar3d_color(s).g == 0.6f, "colour: a set colour wins either way");

        Scatter3DPlot caps = one_point();
        caps.err.cap_lo[0] = std::vector<double>{ 1.0 };
        check(!errorbar3d_translucent(caps),
              "colour: opaque caps alone stay in the opaque pass");
        caps.err.box_lo[0] = std::vector<double>{ 1.0 };
        check(errorbar3d_translucent(caps),
              "colour: a default block is composited");
    }

    // ---- A1: every pixel length is a length in the scene. Two identical
    // bars at different depths under perspective have the same cap in box
    // units, so the far one's cap is shorter on screen by the depth ratio.
    {
        const Projector3D pp(tf, oblique(Projection::Perspective), kFrame, 0.1f);
        auto cap_px = [&](double at, double& depth) {
            Scatter3DPlot s = one_point(at);
            s.err.cap_lo[2] = std::vector<double>{ 1.0 };
            s.opts.errorbar.capsize = 40.0f;
            const auto v = build(s, pp);
            if (v.size() != 3) return 0.0;
            const Px3 a = pp.project_box(v[1].p[0]), b = pp.project_box(v[1].p[1]);
            depth = pp.project(at, 5.0, 5.0).depth;
            const double box_len = length(v[1].p[1] - v[1].p[0]);
            if (std::fabs(box_len - 40.0 * pp.box_units_per_pixel(Vec3{})) > 1e-9) return -1.0;
            return std::hypot(double(a.x - b.x), double(a.y - b.y));
        };
        double d_near = 0.0, d_far = 0.0;
        const double p_a = cap_px(0.5, d_near), p_b = cap_px(9.5, d_far);
        const double near_px_len = d_near < d_far ? p_a : p_b;
        const double far_px_len  = d_near < d_far ? p_b : p_a;
        const double ratio = std::max(d_near, d_far) / std::min(d_near, d_far);
        std::printf("  cap on screen %.2f / %.2f px, depth ratio %.3f\n",
                    near_px_len, far_px_len, ratio);
        check(p_a > 0.0 && p_b > 0.0,
              "geometry (perspective): both caps are capsize box-centre pixels in the box");
        check(ratio > 1.2 && far_px_len > 0.0 &&
              std::fabs(near_px_len / far_px_len - ratio) < ratio * 0.1,
              "geometry (perspective): so a far cap is shorter on screen by the depth ratio");
    }

    // ---- A whisker pointing straight at the eye covers nothing, and is not
    // emitted: a camera looking down x sees an x whisker end-on.
    {
        Camera3D cam;
        cam.azimuth = 0.0; cam.elevation = 0.0;
        const Projector3D end_on(tf, cam, kFrame, 0.1f);
        Scatter3DPlot s = one_point();
        s.err.cap_lo[0] = std::vector<double>{ 2.0 };
        check(build(s, end_on).empty(),
              "geometry: a whisker seen end-on draws nothing");
    }
}

// -------------------------------------------------------------------------
// Both outputs, and the hover
// -------------------------------------------------------------------------
void test_errorbar3d_rendered() {
    std::printf("\n[3D error bars: rendered -- PNG, SVG and hover]\n");

    constexpr int W = 420, H = 360;

    // line3d_render()'s camera: looking down +x, so y is screen-horizontal
    // and z screen-vertical, and pixel runs are easy to state.
    auto bare = [&]() {
        FigureSnapshot fs = make_snapshot3d(1, 1, 1);
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        s->camera.projection = Projection::Orthographic;
        s->camera.azimuth    = 0.0;
        s->camera.elevation  = 0.0;
        s->box_style.panes = false;
        s->grid_enabled = false;
        s->xticks_override = std::vector<Tick>{};
        s->yticks_override = std::vector<Tick>{};
        s->zticks_override = std::vector<Tick>{};
        s->xmin = 0; s->xmax = 10; s->xlim_auto = false;
        s->ymin = 0; s->ymax = 10; s->ylim_auto = false;
        s->zmin = 0; s->zmax = 10; s->zlim_auto = false;
        return fs;
    };
    auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
        GLContext ctx({ .width = W, .height = H, .title = "layout_test", .visible = false });
        NvgRenderer  nvg(ctx.nvg());
        DataRenderer data_r;
        export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
    };
    auto green = [](const unsigned char* p) {
        return p[1] > 110 && p[0] < 60 && p[2] < 60;
    };

    // ---- A flat-capped z whisker in the PNG.
    {
        FigureSnapshot fs = bare();
        Scatter3DPlot s = one_point();
        s.err.cap_lo[2] = std::vector<double>{ 3.0 };
        s.opts.errorbar.color = Color{ 0.0f, 0.6f, 0.0f, 1.0f };
        s.opts.errorbar.linewidth = 4.0f;
        s.opts.errorbar.capsize = 40.0f;
        fs.axes[0].snap3d()->scatter3d.push_back(s);
        render(fs, "eb3d_whisker");

        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load("eb3d_whisker.png", &w, &h, &comp, 4);
        bool stem = false, cap_end = false, cap_beyond = true, not_below = true;
        if (px) {
            auto at = [&](float x, float y) {
                const int xi = static_cast<int>(std::lround(x)), yi = static_cast<int>(std::lround(y));
                if (xi < 0 || xi >= w || yi < 0 || yi >= h) return false;
                return green(px + (yi * w + xi) * 4);
            };
            const Px3 mid = proj.project(5.0, 5.0, 6.5);
            const Px3 top = proj.project(5.0, 5.0, 8.0);
            const Px3 below = proj.project(5.0, 5.0, 1.0);
            stem = at(mid.x, mid.y);
            cap_end = at(top.x - 17.0f, top.y) && at(top.x + 17.0f, top.y);
            cap_beyond = at(top.x - 24.0f, top.y) || at(top.x + 24.0f, top.y);
            not_below = !at(below.x, below.y);
            stbi_image_free(px);
        }
        check(stem, "rendered: the stem is drawn along the whisker");
        check(cap_end && !cap_beyond,
              "rendered: the cap spans `capsize` pixels across the whisker's end, and no further");
        check(not_below, "rendered: and nothing past the offset");
    }

    // ---- The block: translucent by default over a white background, and
    // opaque when both alphas are 1.
    {
        auto block_pixel = [&](float box_alpha, const std::string& stem) {
            FigureSnapshot fs = bare();
            Scatter3DPlot s = one_point();
            s.err.box_lo[1] = std::vector<double>{ 2.0 };
            s.err.box_lo[2] = std::vector<double>{ 2.0 };
            s.opts.errorbar.color = Color::Red;
            s.opts.errorbar.box_alpha = box_alpha;
            s.opts.errorbar.edge_alpha = 1.0f;
            fs.axes[0].snap3d()->scatter3d.push_back(s);
            render(fs, stem);
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            std::array<int, 3> rgb{ -1, -1, -1 };
            unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
            if (px) {
                const Px3 c = proj.project(5.0, 6.0, 6.0);   // inside the block, off the stem
                const int xi = static_cast<int>(std::lround(c.x)), yi = static_cast<int>(std::lround(c.y));
                if (xi >= 0 && xi < w && yi >= 0 && yi < h)
                    for (int i = 0; i < 3; ++i) rgb[i] = px[(yi * w + xi) * 4 + i];
                stbi_image_free(px);
            }
            return rgb;
        };
        const auto solid = block_pixel(1.0f, "eb3d_block_solid");
        const auto glass = block_pixel(0.25f, "eb3d_block_glass");
        std::printf("  block interior: solid %d,%d,%d  default %d,%d,%d\n",
                    solid[0], solid[1], solid[2], glass[0], glass[1], glass[2]);
        auto byte = [](float v) { return static_cast<int>(std::lround(v * 255.0f)); };
        check(std::abs(solid[0] - byte(Color::Red.r)) <= 3 &&
              std::abs(solid[1] - byte(Color::Red.g)) <= 3 &&
              std::abs(solid[2] - byte(Color::Red.b)) <= 3,
              "rendered: an opaque block fills its interior with its colour");
        // Two faces overlap along this view (front and back), each at 0.25.
        check(glass[0] > glass[1] + 50 && glass[1] > 100 && glass[1] < 200 && std::abs(glass[1] - glass[2]) <= 2,
              "rendered: the default block is translucent -- tinted, not filled");
    }

    // ---- The SVG: strokes as <line>, faces as translucent <polygon>. Two
    // scenes, because a stem running through its own block is correctly cut
    // by the painter where it crosses the block's faces, which would make a
    // stroke count in one scene a count of pieces.
    {
        const std::string g = "stroke=\"rgb(0,153,0)\"";
        auto scene = [&](const Scatter3DPlot& s, const std::string& path) {
            FigureSnapshot fs = bare();
            RenderSnapshot3D* s3 = fs.axes[0].snap3d();
            s3->camera.azimuth = -55.0;
            s3->camera.elevation = 24.0;
            s3->scatter3d.push_back(s);
            export_figure_svg(fs, path, W, H);
            return read_text(path);
        };

        Scatter3DPlot whisker = one_point();
        whisker.err.cap_lo[2] = std::vector<double>{ 3.0 };
        whisker.opts.errorbar.color = Color{ 0.0f, 0.6f, 0.0f, 1.0f };
        whisker.opts.errorbar.capstyle = CapStyle::Arrow;
        const std::string ws = scene(whisker, "eb3d_scene_whisker.svg");
        check(count_elements(ws, "<line ", g) == 5 &&
              count_elements(ws, "<line ", g, "stroke-opacity") == 0,
              "svg: a chevron-capped whisker is five opaque strokes");
        check(count_elements(ws, "<line ", g, "stroke-linecap") == 0,
              "svg: error-bar strokes are butt-ended, as 2D whiskers are");

        Scatter3DPlot block = one_point();
        block.err.box_lo[0] = std::vector<double>{ 1.0 };
        block.opts.errorbar.color = Color{ 0.0f, 0.6f, 0.0f, 1.0f };
        const std::string bs = scene(block, "eb3d_scene_block.svg");
        check(count_elements(bs, "<line ", g, "stroke-opacity=\"0.5\"") == 12 &&
              count_elements(bs, "<line ", g) == 12,
              "svg: a block's twelve edges, at edge_alpha");
        check(count_elements(bs, "<polygon ", "fill=\"rgb(0,153,0)\"",
                             "fill-opacity=\"0.25\"") == 6,
              "svg: and its six faces at box_alpha");

        // Both together: the painter orders the stem against the faces it
        // passes through, so it is cut into pieces rather than drawn over or
        // under the whole block -- and every piece is still an opaque stroke.
        Scatter3DPlot both = whisker;
        both.err.box_lo[0] = std::vector<double>{ 1.0 };
        const std::string cs = scene(both, "eb3d_scene_both.svg");
        const std::size_t opaque = count_elements(cs, "<line ", g) -
                                   count_elements(cs, "<line ", g, "stroke-opacity");
        std::printf("  whisker through its block: %zu opaque stroke pieces\n", opaque);
        // Splitting is the painter's; the SEXTANT_NEWELL=0 control has none.
        bool newell_on = true;
        if (const char* env = std::getenv("SEXTANT_NEWELL"))
            newell_on = std::atoi(env) != 0;
        if (newell_on)
            check(opaque > 5,
                  "svg: a stem through its own block is split where it crosses the faces");
        else
            check(opaque == 5,
                  "svg (control): the whole-object order emits the stem unsplit");
    }

    // ---- Hover: the offsets beside each coordinate.
    {
        const Transform3D tf = ten_box();
        const Projector3D proj(tf, oblique(Projection::Orthographic), kFrame, 0.1f);
        RenderSnapshot3D snap;
        Scatter3DPlot s = one_point();
        s.opts.marker = MarkerStyle::Circle;
        s.err.cap_lo[2] = std::vector<double>{ 0.5 };
        s.err.box_lo[0] = std::vector<double>{ 1.0 };
        s.err.box_hi[0] = std::vector<double>{ 2.0 };
        snap.scatter3d.push_back(s);
        const Px3 q = proj.project(5.0, 5.0, 5.0);
        const auto r = find_hint3d(snap, proj, q.x, q.y);
        const std::string want = "x=5 box +2/-1, y=5, z=5 cap \xC2\xB1" "0.5";
        check(r && r->text == want,
              "hover: a point's text carries each axis's offsets beside its coordinate");
        if (r && r->text != want) std::printf("  got \"%s\"\n", r->text.c_str());

        Line3DPlot l;
        l.x = std::vector<double>{ 5.0, 8.0 };
        l.y = std::vector<double>{ 5.0, 8.0 };
        l.z = std::vector<double>{ 5.0, 8.0 };
        l.colors = std::vector<double>{ 1.5, 2.0 };
        l.err.cap_hi[1] = std::vector<double>{ 0.25, 0.25 };
        RenderSnapshot3D ls;
        ls.lines3d.push_back(l);
        const auto lr = find_hint3d(ls, proj, q.x, q.y);
        check(lr && lr->text == "x=5, y=5 cap \xC2\xB1" "0.25, z=5\nc=1.5",
              "hover: a path's vertex does too, with its colour line after");
    }
}

} // namespace lt
