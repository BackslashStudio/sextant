// The vector scene: Newell's algorithm, splits, and the export budget. Part
// of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // -------------------------------------------------------------------------
    // Newell's algorithm
    // -------------------------------------------------------------------------
    // Oracle: a ray cast per pixel (Projector3D::ray_from_pixel()); wherever a ray
    // meets several emitted polygons, the emission order must run back to front.
    // Nothing the algorithm computed is used.
    //
    //   1. Two crossing quads: a correct result must contain a split.
    //   2. A pinwheel of three quads: no total order exists; splitting resolves it.
    //   3. Two separated quads: nothing may be split, and the order is depth order.
    // -------------------------------------------------------------------------
    // The gallery's interleaving cells (test_translucent3d), under the same oracle.
    // Surface cells are emitted as two triangles: a warped quad has no plane, so
    // Newell's tests never resolved it.
    void test_scene3d_svg_order() {
        std::printf("\n[3D: the gallery's interleaving cells, ordered for the SVG]\n");

        using namespace sextant;

        constexpr int W = 460, H = 420;
        constexpr int NU = 13, NV = 13;
        std::vector<double> gx(NU), gy(NV), ripple(NU * NV), bowl(NU * NV), midway(NU * NV);
        for (int i = 0; i < NU; ++i) gx[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (NU - 1);
        for (int j = 0; j < NV; ++j) gy[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (NV - 1);
        for (int i = 0; i < NU; ++i)
            for (int j = 0; j < NV; ++j) {
                const double x = gx[static_cast<std::size_t>(i)], y = gy[static_cast<std::size_t>(j)];
                const double r = std::hypot(x, y);
                const double v = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
                const std::size_t k = static_cast<std::size_t>(i * NV + j);
                ripple[k] = v;
                bowl[k] = 0.22 * (x * x - y * y);
                midway[k] = -1.5 + 0.5 * v;
            }

        auto base = [&] {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -55.0;
            s->camera.elevation = 24.0;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            return fs;
        };
        auto add_surface = [&](RenderSnapshot3D* s, const std::vector<double>& h) {
            SurfacePlot sp;
            sp.u = gx;
            sp.v = gy;
            sp.heights = h;
            sp.opts.colormap = true;
            sp.opts.alpha = 0.6f;
            s->surfaces.push_back(std::move(sp));
        };
        auto add_plane = [&](RenderSnapshot3D* s, double off) {
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = off;
            pl.opts.alpha = 0.6f;
            HeatmapPlot hp;
            hp.rows = 24;
            hp.cols = 24;
            std::vector<float> hv(576, 0.0f);
            for (int r = 0; r < 24; ++r)
                for (int c = 0; c < 24; ++c)
                    hv[static_cast<std::size_t>(r) * 24 + c] =
                            static_cast<float>(std::sin(0.4 * c) * std::cos(0.4 * r));
            hp.data = CowVec<float>(std::move(hv));
            hp.xrange = {-3.0, 3.0};
            hp.yrange = {-3.0, 3.0};
            pl.sheet.heatmaps.push_back(std::move(hp));
            s->planes.push_back(std::move(pl));
        };

        // What the writer emits: (pixel ring, screen bbox, parent's box-space plane).
        struct Emitted {
            std::vector<float> xy;
            float bb[4];
            Vec3 p0, n;
            bool ok = false;
        };

        auto run = [&](const char* what, FigureSnapshot& fs) {
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            const std::vector<Bar3DPolygon> bp = plan_bars3d(pj, s->bars3d);
            const std::vector<Surface3DPolygon> sp = plan_surfaces3d(pj, s->surfaces);
            const std::vector<PlanePlanItem> pp = plan_planes3d(pj, s->planes);
            PaintOrderStats st;
            const std::vector<ScenePaint> scene = plan_scene3d(pj, bp, sp, pp, {}, {}, {}, {}, &st);

            std::vector<Emitted> em;
            em.reserve(scene.size());
            for (const ScenePaint& e: scene) {
                Emitted x;
                const std::vector<Vec3>* parent = nullptr;
                std::vector<Vec3> quad;
                if (e.kind == ScenePaint::Kind::Bar && e.index < bp.size()) {
                    x.xy = e.xy.empty() ? bp[e.index].xy : e.xy;
                    parent = &bp[e.index].box;
                } else if (e.kind == ScenePaint::Kind::Surface && e.index < sp.size()) {
                    x.xy = e.xy.empty() ? sp[e.index].xy : e.xy;
                    parent = &sp[e.index].box;
                } else if (e.kind == ScenePaint::Kind::Plane) {
                    for (const PlanePlanItem& it: pp)
                        if (it.plane == e.index) {
                            quad.assign(it.quad, it.quad + 4);
                            break;
                        }
                    parent = &quad;
                    if (!e.xy.empty()) x.xy = e.xy;
                    else {
                        std::vector<Px3> r;
                        pj.project_polygon(quad, r);
                        for (const Px3& q: r) {
                            x.xy.push_back(q.x);
                            x.xy.push_back(q.y);
                        }
                    }
                }
                x.bb[0] = x.bb[1] = 1e30f;
                x.bb[2] = x.bb[3] = -1e30f;
                for (std::size_t i = 0; i + 1 < x.xy.size(); i += 2) {
                    x.bb[0] = std::min(x.bb[0], x.xy[i]);
                    x.bb[1] = std::min(x.bb[1], x.xy[i + 1]);
                    x.bb[2] = std::max(x.bb[2], x.xy[i]);
                    x.bb[3] = std::max(x.bb[3], x.xy[i + 1]);
                }
                if (parent && parent->size() >= 3 && x.xy.size() >= 6
                    && ring_plane(*parent, x.p0, x.n))
                    x.ok = true;
                em.push_back(std::move(x));
            }

            auto inside = [](const Emitted& e, float px, float py) {
                if (!e.ok || px < e.bb[0] || px > e.bb[2] || py < e.bb[1] || py > e.bb[3])
                    return false;
                const std::size_t n = e.xy.size() / 2;
                int sign = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const float ex = e.xy[j * 2] - e.xy[i * 2];
                    const float ey = e.xy[j * 2 + 1] - e.xy[i * 2 + 1];
                    const float cr = ex * (py - e.xy[i * 2 + 1]) - ey * (px - e.xy[i * 2]);
                    if (std::fabs(cr) < 1e-6f) continue;
                    const int sg = cr > 0 ? 1 : -1;
                    if (sign == 0) sign = sg;
                    else if (sg != sign) return false;
                }
                return sign != 0;
            };

            const PlotRect& fr = lay.cells[0].frame;
            int overlapped = 0, wrong = 0;
            double worst_gap = 0.0;
            int wrong_big = 0;
            for (float y = fr.y; y < fr.y + fr.h; y += 3.0f)
                for (float x = fr.x; x < fr.x + fr.w; x += 3.0f) {
                    float prev = 0.0f;
                    bool have = false, bad = false, bad_big = false;
                    int hits = 0;
                    for (const Emitted& e: em) {
                        if (!inside(e, x, y)) continue;
                        const Projector3D::Ray3 r = pj.ray_from_pixel(x, y);
                        const double den = dot(e.n, r.dir);
                        if (std::fabs(den) < 1e-12) continue;
                        const double t = dot(e.n, e.p0 - r.origin) / den;
                        const float d = pj.project_box(r.origin + r.dir * t).depth;
                        ++hits;
                        if (have && d > prev) {
                            worst_gap = std::max(worst_gap, (double) (d - prev));
                            if (d > prev + 3e-3f) bad = true;
                            if (d > prev + 3e-2f) bad_big = true;
                        }
                        prev = d;
                        have = true;
                    }
                    if (hits > 1) ++overlapped;
                    if (bad) ++wrong;
                    if (bad_big) ++wrong_big;
                }
            std::printf("  %-16s %zu polys -> %zu, %zu splits, %zu tests, bailed=%d | "
                        "%zu unresolved | %d px with two or more layers, %d out of order "
                        "(%d by >0.03; worst gap %.4f)\n",
                        what, st.input, st.output, st.splits, st.tests, st.bailed ? 1 : 0,
                        st.unresolved, overlapped, wrong, wrong_big, worst_gap);
            check(overlapped > 500, std::string(what) + ": the two objects really do interleave");

            bool newell_on = true;
            if (const char* env = std::getenv("SEXTANT_NEWELL"))
                newell_on = std::atoi(env) != 0;
            if (newell_on) {
                check(!st.bailed,
                      std::string(what) + ": the painter finishes inside its work bound");
                // Zero, not "nearly all".
                check(wrong == 0,
                      std::string(what) + ": every pixel's layers are emitted back to front");
            } else {
                check(st.splits == 0 && wrong > 20,
                      std::string(what) + " (control): the whole-object order splits nothing "
                      "and gets this cell wrong at dozens of pixels");
            }
        }; {
            // The cell whose plane the surface crosses.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            add_surface(s, ripple);
            add_plane(s, 0.35);
            run("plane x surface", fs);
        } {
            // The cell whose sheet runs inside the bars: no camera has a
            // whole-object answer for it, and it is the densest scene here.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            Bar3DPlot b;
            b.u = gx;
            b.v = gy;
            b.heights = ripple;
            b.u_width = b.v_width = 0.35;
            b.opts.alpha = 0.5f;
            b.opts.bottom = -1.5;
            s->bars3d.push_back(std::move(b));
            add_surface(s, midway);
            run("bar x surface", fs);
        } {
            // The same cell at the **default** camera, which is a different scene
            // and not a redundant one.
            //
            // The camera (-60, 30) matters: there, coplanar split pieces used to
            // "cut" a bar face into itself repeatedly; this is the gallery's view.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.azimuth = -60.0;
            s->camera.elevation = 30.0;
            Bar3DPlot b;
            b.u = gx;
            b.v = gy;
            b.heights = ripple;
            b.u_width = b.v_width = 0.35;
            b.opts.alpha = 0.5f;
            b.opts.bottom = -1.5;
            s->bars3d.push_back(std::move(b));
            add_surface(s, midway);
            run("bar x surface @def", fs);
        } {
            // Two sheets crossing along two curves -- the cell that was already
            // right, kept so a change that fixes the other two by breaking this
            // one has to say so.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            add_surface(s, ripple);
            add_surface(s, bowl);
            run("surface x surface", fs);
        }

        // Control for the other side of the overlap tolerance: a smooth sheet
        // alone occludes itself nowhere, so it must come out uncut in
        // surface_draw_order()'s order (it splits at slack 0.10, not at 0.15).
        {
            constexpr int M = 50;
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            std::vector<double> gu(M), gv(M), h(M * M);
            for (int i = 0; i < M; ++i) gu[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (M - 1);
            for (int j = 0; j < M; ++j) gv[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (M - 1);
            for (int i = 0; i < M; ++i)
                for (int j = 0; j < M; ++j)
                    h[static_cast<std::size_t>(i * M + j)] =
                            0.6 * std::sin(gu[static_cast<std::size_t>(i)])
                            * std::cos(gv[static_cast<std::size_t>(j)]);
            SurfacePlot sp;
            sp.u = gu;
            sp.v = gv;
            sp.heights = CowVec<double>(std::move(h));
            sp.opts.colormap = true;
            s->surfaces.push_back(std::move(sp));

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;
            PaintOrderStats st;
            plan_scene3d(pj, {}, plan_surfaces3d(pj, s->surfaces), {}, {}, {}, {}, {}, &st);
            std::printf("  lone smooth sheet %zu polys -> %zu, %zu splits\n",
                        st.input, st.output, st.splits);
            bool newell_on = true;
            if (const char* env = std::getenv("SEXTANT_NEWELL"))
                newell_on = std::atoi(env) != 0;
            if (newell_on)
                check(st.input > 4000 && st.splits == 0 && st.output == st.input,
                      "lone sheet: a surface that occludes itself nowhere is not cut");
        }
    }

    // -------------------------------------------------------------------------
    // A surface's wireframe in the SVG
    // -------------------------------------------------------------------------
    // The wireframe is two-point strokes (a four-point cell outline has no plane).
    // The ray-cast oracle can't see strokes, so this checks: wherever a polygon
    // covers a point of a line, the polygon is drawn after the line exactly when
    // it is nearer. Also: no black filled cells.
    void test_scene3d_svg_wireframe() {
        std::printf("\n[3D: a surface's wireframe, ordered and drawn in the SVG]\n");

        using namespace sextant;

        constexpr int W = 460, H = 420;
        constexpr int N = 13;
        const Color kWire{1.0f, 0.0f, 0.0f, 1.0f};
        std::vector<double> g(N), ripple(N * N), bowl(N * N), midway(N * N);
        for (int i = 0; i < N; ++i) g[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (N - 1);
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                const double x = g[static_cast<std::size_t>(i)], y = g[static_cast<std::size_t>(j)];
                const double r = std::hypot(x, y);
                const double v = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
                const std::size_t k = static_cast<std::size_t>(i * N + j);
                ripple[k] = v;
                bowl[k] = 0.22 * (x * x - y * y);
                midway[k] = -1.5 + 0.5 * v;
            }
        // Every edge of an N x N sample grid, once.
        constexpr std::size_t kEdges = 2u * N * (N - 1);

        auto base = [&] {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            // Orthographic, so depth along a projected line is linear and a point
            // on a piece maps back to box space exactly.
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -60.0;
            s->camera.elevation = 30.0;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            return fs;
        };
        auto add_surface = [&](RenderSnapshot3D* s, const std::vector<double>& h, float alpha) {
            SurfacePlot sp;
            sp.u = g;
            sp.v = g;
            sp.heights = h;
            sp.opts.colormap = true;
            sp.opts.alpha = alpha;
            sp.opts.edges = true;
            sp.opts.edgecolor = kWire;
            sp.opts.edge_linewidth = 1.0f;
            s->surfaces.push_back(std::move(sp));
        };
        auto add_bars = [&](RenderSnapshot3D* s, float alpha) {
            Bar3DPlot b;
            b.u = g;
            b.v = g;
            b.heights = ripple;
            b.u_width = b.v_width = 0.35;
            b.opts.color = Color::Orange;
            b.opts.alpha = alpha;
            b.opts.bottom = -1.5;
            s->bars3d.push_back(std::move(b));
        };

        auto run = [&](const char* what, FigureSnapshot& fs) {
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            const std::vector<Bar3DPolygon> bp = plan_bars3d(pj, s->bars3d);
            const std::vector<Surface3DPolygon> sp = plan_surfaces3d(pj, s->surfaces);

            // The plan: one two-point stroke per grid edge.
            std::vector<std::size_t> per_plot(s->surfaces.size(), 0);
            bool shapes_ok = true;
            for (const Surface3DPolygon& p: sp) {
                if (p.filled) continue;
                ++per_plot[p.plot];
                if (p.box.size() != 2 || p.xy.size() != 4) shapes_ok = false;
            }
            check(shapes_ok, std::string(what) + ": a wireframe edge is a two-point line, not a ring");
            bool counts_ok = true;
            for (std::size_t c: per_plot) counts_ok = counts_ok && c == kEdges;
            check(counts_ok, std::string(what) + ": every grid edge is emitted exactly once");

            PaintOrderStats st;
            const std::vector<ScenePaint> scene = plan_scene3d(pj, bp, sp, {}, {}, {}, {}, {}, &st);

            // Every emitted polygon, with its parent's plane.
            struct Poly {
                std::size_t at;
                std::vector<float> xy;
                Vec3 p0, n;
            };
            struct Line {
                std::size_t at;
                float x0, y0, x1, y1;
                std::size_t src;
            };
            std::vector<Poly> polys;
            std::vector<Line> lines;
            for (std::size_t at = 0; at < scene.size(); ++at) {
                const ScenePaint& e = scene[at];
                const std::vector<float>* xy = nullptr;
                const std::vector<Vec3>* ring = nullptr;
                bool filled = true;
                if (e.kind == ScenePaint::Kind::Bar) {
                    xy = e.xy.empty() ? &bp[e.index].xy : &e.xy;
                    ring = &bp[e.index].box;
                    filled = bp[e.index].filled;
                } else if (e.kind == ScenePaint::Kind::Surface) {
                    xy = e.xy.empty() ? &sp[e.index].xy : &e.xy;
                    ring = &sp[e.index].box;
                    filled = sp[e.index].filled;
                } else continue;
                if (!filled) {
                    if (e.kind == ScenePaint::Kind::Surface && xy->size() == 4)
                        lines.push_back({at, (*xy)[0], (*xy)[1], (*xy)[2], (*xy)[3], e.index});
                    continue;
                }
                Poly p{at, *xy, {}, {}};
                if (xy->size() >= 6 && ring_plane(*ring, p.p0, p.n)) polys.push_back(std::move(p));
            }

            // Strictly inside by a margin (points on shared boundaries have no
            // order).
            auto inside = [](const std::vector<float>& xy, float px, float py) {
                const std::size_t n = xy.size() / 2;
                int sign = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const float ex = xy[j * 2] - xy[i * 2], ey = xy[j * 2 + 1] - xy[i * 2 + 1];
                    const float len = std::sqrt(ex * ex + ey * ey);
                    if (len < 1e-6f) continue;
                    const float cr = (ex * (py - xy[i * 2 + 1]) - ey * (px - xy[i * 2])) / len;
                    if (std::fabs(cr) < 0.75f) return false;
                    const int sg = cr > 0 ? 1 : -1;
                    if (sign == 0) sign = sg;
                    else if (sg != sign) return false;
                }
                return sign != 0;
            };

            int covered = 0, wrong = 0;
            for (const Line& l: lines) {
                const Surface3DPolygon& parent = sp[l.src];
                const float ax = parent.xy[0], ay = parent.xy[1];
                const float dx = parent.xy[2] - ax, dy = parent.xy[3] - ay;
                const float d2 = dx * dx + dy * dy;
                if (d2 < 1e-6f) continue;
                for (const float t: {0.2f, 0.5f, 0.8f}) {
                    const float px = l.x0 + (l.x1 - l.x0) * t, py = l.y0 + (l.y1 - l.y0) * t;
                    const double u = ((px - ax) * dx + (py - ay) * dy) / d2;
                    const Vec3 bpnt = parent.box[0] + (parent.box[1] - parent.box[0]) * u;
                    const float ld = pj.project_box(bpnt).depth;
                    bool bad = false;
                    for (const Poly& p: polys) {
                        if (!inside(p.xy, px, py)) continue;
                        const Projector3D::Ray3 r = pj.ray_from_pixel(px, py);
                        const double den = dot(p.n, r.dir);
                        if (std::fabs(den) < 1e-12) continue;
                        const double tt = dot(p.n, p.p0 - r.origin) / den;
                        const float pd = pj.project_box(r.origin + r.dir * tt).depth;
                        ++covered;
                        // Larger depth is farther: later polygons must be nearer.
                        if (p.at > l.at && pd > ld + 3e-3f) bad = true;
                        if (p.at < l.at && pd < ld - 3e-3f) bad = true;
                    }
                    if (bad) ++wrong;
                }
            }
            std::printf("  %-22s %zu polys -> %zu, %zu splits, %zu tests, bailed=%d | "
                        "%zu strokes emitted, %d line points under a polygon, %d misordered\n",
                        what, st.input, st.output, st.splits, st.tests, st.bailed ? 1 : 0,
                        lines.size(), covered, wrong);

            bool newell_on = true;
            if (const char* env = std::getenv("SEXTANT_NEWELL"))
                newell_on = std::atoi(env) != 0;
            if (newell_on) {
                check(!st.bailed, std::string(what) + ": the painter finishes inside its bound");
                check(covered > 200, std::string(what) + ": the lines really do pass under polygons");
                check(wrong == 0, std::string(what) + ": every line is drawn between what is "
                                  "behind it and what is in front of it");
            }

            // Through the writer: no black cells, and the wire is lines.
            SvgSaveReport rep;
            const std::string path = "surface_wire_order.svg";
            export_figure_svg(fs, path, W, H, {}, &rep);
            std::ifstream f(path, std::ios::binary);
            std::string line;
            int black = 0, red_polygons = 0, red_lines = 0;
            while (std::getline(f, line)) {
                const bool polygon = line.find("<polygon") != std::string::npos;
                if (polygon && line.find("fill=\"rgb(0,0,0)\"") != std::string::npos) ++black;
                if (line.find("stroke=\"rgb(255,0,0)\"") != std::string::npos)
                    (polygon ? red_polygons : red_lines)++;
            }
            f.close();
            std::filesystem::remove(path);
            std::printf("  %-22s svg: exact=%d, %d black polygons, %d wire polylines, "
                        "%d wire polygons\n", what, rep.scene_order_exact ? 1 : 0,
                        black, red_lines, red_polygons);
            if (newell_on)
                check(rep.scene_order_exact, std::string(what) + ": the SVG export does not bail");
            check(black == 0, std::string(what) + ": no wireframe item is filled black");
            check(red_polygons == 0 &&
                  red_lines >= static_cast<int>(kEdges * s->surfaces.size()),
                  std::string(what) + ": the wireframe reaches the file as open lines");
        }; {
            // The report's own scene: translucent sheets, wireframe on.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            add_surface(s, ripple, 0.6f);
            add_surface(s, bowl, 0.6f);
            run("surface x surface", fs);
        } {
            // A translucent sheet through translucent bars.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            add_bars(s, 0.5f);
            add_surface(s, midway, 0.6f);
            run("bar x surface", fs);
        } {
            // Opaque, which is where a line ordered by its depth extent alone
            // would show: through the face of a solid bar.
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            add_bars(s, 1.0f);
            add_surface(s, midway, 1.0f);
            run("opaque bar x surface", fs);
        } {
            // The lone-sheet control, with the wire on: a height field that
            // occludes itself nowhere still has nothing to cut.
            constexpr int M = 50;
            FigureSnapshot fs = base();
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            std::vector<double> gu(M), h(M * M);
            for (int i = 0; i < M; ++i) gu[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (M - 1);
            for (int i = 0; i < M; ++i)
                for (int j = 0; j < M; ++j)
                    h[static_cast<std::size_t>(i * M + j)] =
                            0.6 * std::sin(gu[static_cast<std::size_t>(i)])
                            * std::cos(gu[static_cast<std::size_t>(j)]);
            SurfacePlot sp;
            sp.u = gu;
            sp.v = gu;
            sp.heights = CowVec<double>(std::move(h));
            sp.opts.colormap = true;
            sp.opts.edges = true;
            sp.opts.edgecolor = kWire;
            s->surfaces.push_back(std::move(sp));
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;
            PaintOrderStats st;
            plan_scene3d(pj, {}, plan_surfaces3d(pj, s->surfaces), {}, {}, {}, {}, {}, &st);
            std::printf("  lone wired sheet %zu polys -> %zu, %zu splits\n",
                        st.input, st.output, st.splits);
            bool newell_on = true;
            if (const char* env = std::getenv("SEXTANT_NEWELL"))
                newell_on = std::atoi(env) != 0;
            if (newell_on)
                check(st.splits == 0 && st.output == st.input,
                      "lone wired sheet: the wireframe adds no cuts to a sheet that needs none");
        }
    }

    // -------------------------------------------------------------------------
    // Export bounds as options, and the report when one binds
    // -------------------------------------------------------------------------
    // Through the public Figure::savefig_svg(): the budget is obeyed, the report
    // says it was hit, and the warning names the bound. Headless (no GL).
    void test_export_budget() {
        std::printf("\n[3D: the export budget, and what happens when it binds]\n");

        using namespace sextant;

        const std::string path = "budget_probe.svg";
        // A YZ plane cutting a bar grid; needs a handful of cuts.
        auto make = [] {
            auto fig = Figure::create({
                .width = 420, .height = 360,
                .title = "budget", .vsync = false
            });
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->set_view(-55.0, 24.0);
            // An odd grid, so a bar is centred on x = 0 and gets cut (an even grid
            // needs no splits).
            std::vector<double> u(5), v(5), z(25);
            for (int i = 0; i < 5; ++i) u[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / 4;
            for (int j = 0; j < 5; ++j) v[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / 4;
            for (std::size_t k = 0; k < z.size(); ++k)
                z[k] = 0.6 + 0.4 * std::sin(static_cast<double>(k));
            ax->bar3d(PlaneOrientation::XY, u, v, z,
                      {
                          .color = Color::Orange, .width = 0.7f, .depth = 0.7f,
                          .bottom = -1.0
                      });
            // A heatmap on the plane (an empty plane emits nothing to order).
            std::vector<double> m(16 * 16);
            for (std::size_t k = 0; k < m.size(); ++k)
                m[k] = std::sin(0.4 * static_cast<double>(k));
            ax->plane(PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
                    ->heatmap(m, 16, 16, {-3.0, 3.0}, {-1.0, 1.2});
            return fig;
        };

        // Under SEXTANT_NEWELL=0 there is no painter: the budget must be inert.
        bool newell_on = true;
        if (const char* env = std::getenv("SEXTANT_NEWELL"))
            newell_on = std::atoi(env) != 0;
        if (!newell_on) {
            const SvgSaveReport off = make()->savefig_svg(path, {.max_splits = 1});
            std::printf("  SEXTANT_NEWELL=0  %zu splits, exact=%d\n",
                        off.splits, off.scene_order_exact ? 1 : 0);
            check(off.scene_order_exact && off.splits == 0 && off.warning.empty(),
                  "budget (control): with no painter there is no bound to hit");
            std::filesystem::remove(path);
            return;
        }

        // 1. The default finishes this scene and reports so.
        const SvgSaveReport ok = make()->savefig_svg(path);
        std::printf("  default        %zu splits, %zu tests, exact=%d\n",
                    ok.splits, ok.tests, ok.scene_order_exact ? 1 : 0);
        check(ok.scene_order_exact, "budget: the default finishes a scene it can finish");
        check(ok.warning.empty(), "budget: an exact export says nothing");
        check(ok.splits > 0, "budget: the scene really does need cutting");

        // 2. A budget below the need binds and is reported; the file is still
        //    written.
        const SvgSaveReport tight =
                make()->savefig_svg(path, {.max_splits = 2});
        std::printf("  max_splits=2   %zu splits, exact=%d\n",
                    tight.splits, tight.scene_order_exact ? 1 : 0);
        check(!tight.scene_order_exact, "budget: a budget below the need is honoured");
        check(tight.splits <= 3, "budget: it stops at the budget rather than near it");
        check(tight.warning.find("max_splits") != std::string::npos,
              "budget: the warning names the bound that bound");
        check(std::filesystem::exists(path) && std::filesystem::file_size(path) > 0,
              "budget: a bailed export still writes the file");

        // 3. The other bound is separately reachable and named.
        const SvgSaveReport slow =
                make()->savefig_svg(path, {.max_tests = 4});
        std::printf("  max_tests=4    %zu tests, exact=%d\n",
                    slow.tests, slow.scene_order_exact ? 1 : 0);
        check(!slow.scene_order_exact, "budget: the test bound is reachable on its own");
        check(slow.warning.find("max_tests") != std::string::npos,
              "budget: the warning distinguishes the two bounds");

        // 4. Raising a bound past the need matches the default (more work, never
        //    a different picture).
        const SvgSaveReport loose =
                make()->savefig_svg(path, {.max_splits = 1000000, .max_tests = 100000000});
        check(loose.scene_order_exact && loose.splits == ok.splits,
              "budget: a budget above the need changes nothing");
        std::filesystem::remove(path);
    }

    void test_painter3d() {
        std::printf("\n[3D: Newell's algorithm and the splits it needs (step 9)]\n");

        using namespace sextant;

        constexpr int W = 360, H = 300;

        FigureSnapshot fs = make_snapshot3d(1, 1, 1); {
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -55.0;
            s->camera.elevation = 28.0;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            s->xmin = 0;
            s->xmax = 1;
            s->xlim_auto = false;
            s->ymin = 0;
            s->ymax = 1;
            s->ylim_auto = false;
            s->zmin = 0;
            s->zmax = 1;
            s->zlim_auto = false;
        }
        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;
        const Transform3D& tf = proj.transform();

        // A quad from four data-space corners, as its own object.
        auto quad = [&](std::size_t object, std::initializer_list<std::array<double, 3>> pts) {
            PaintPoly p;
            p.kind = PaintPoly::Kind::Surface;
            p.object = object;
            p.element = 0;
            p.rank = 0;
            p.source = object;
            for (const auto& c: pts) p.ring.push_back(tf.to_box(c[0], c[1], c[2]));
            return p;
        };

        // Depth (Px3::depth) where a pixel's ray meets a polygon's plane, or false
        // if it misses (point-in-polygon in projection).
        auto ray_hit = [&](const PaintPoly& p, float px, float py, float& depth) {
            if (p.px.size() < 6) return false;
            const std::size_t n = p.px.size() / 2;
            int sign = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                const float ex = p.px[j * 2] - p.px[i * 2];
                const float ey = p.px[j * 2 + 1] - p.px[i * 2 + 1];
                const float cx = px - p.px[i * 2];
                const float cy = py - p.px[i * 2 + 1];
                const float cr = ex * cy - ey * cx;
                if (std::fabs(cr) < 1e-6f) continue;
                const int s = cr > 0 ? 1 : -1;
                if (sign == 0) sign = s;
                else if (s != sign) return false;
            }
            Vec3 p0{}, nrm{};
            if (!ring_plane(p.ring, p0, nrm)) return false;
            const Projector3D::Ray3 r = proj.ray_from_pixel(px, py);
            const double denom = dot(nrm, r.dir);
            if (std::fabs(denom) < 1e-12) return false;
            const double t = dot(nrm, p0 - r.origin) / denom;
            const Vec3 hit = r.origin + r.dir * t;
            depth = proj.project_box(hit).depth;
            return true;
        };

        // The oracle: over a pixel grid, wherever a ray meets several emitted
        // polygons the later one is nearer. `slack` forgives pixels on split seams.
        auto check_order = [&](const std::vector<PaintPoly>& out, const char* what) {
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            for (const PaintPoly& p: out) {
                x0 = std::min(x0, p.bb[0]);
                y0 = std::min(y0, p.bb[1]);
                x1 = std::max(x1, p.bb[2]);
                y1 = std::max(y1, p.bb[3]);
            }
            int probed = 0, overlapped = 0, wrong = 0;
            for (float y = y0; y <= y1; y += 1.0f)
                for (float x = x0; x <= x1; x += 1.0f) {
                    float prev = 0.0f;
                    bool have = false;
                    bool bad = false;
                    int hits = 0;
                    for (const PaintPoly& p: out) {
                        float d = 0.0f;
                        if (!ray_hit(p, x, y, d)) continue;
                        ++hits;
                        // Back to front: depth must not increase.
                        if (have && d > prev + 1e-4f) bad = true;
                        prev = d;
                        have = true;
                    }
                    ++probed;
                    if (hits > 1) ++overlapped;
                    if (bad) ++wrong;
                }
            std::printf("  %-22s %d px probed, %d with two or more layers, %d out of order\n",
                        what, probed, overlapped, wrong);
            check(overlapped > 100, std::string(what) + ": the scene actually overlaps");
            check(wrong == 0, std::string(what) + ": every pixel's layers come out back to front");
        };

        // ---- 1. Two crossing quads: no whole-polygon order exists
        {
            std::vector<PaintPoly> in;
            in.push_back(quad(0, {
                                  {0.1, 0.5, 0.15}, {0.9, 0.5, 0.15},
                                  {0.9, 0.5, 0.85}, {0.1, 0.5, 0.85}
                              })); // upright, y = 0.5
            in.push_back(quad(1, {
                                  {0.5, 0.1, 0.15}, {0.5, 0.9, 0.15},
                                  {0.5, 0.9, 0.85}, {0.5, 0.1, 0.85}
                              })); // upright, x = 0.5
            PaintOrderStats st;
            const std::vector<PaintPoly> out = paint_order(in, proj, &st);
            std::printf("  crossing quads: %zu in, %zu out, %zu splits, %zu cycles, %zu tests\n",
                        st.input, st.output, st.splits, st.cycles, st.tests);
            check(st.splits >= 1 && out.size() > in.size(),
                  "newell: two crossing quads cannot be ordered without a split");
            check(!st.bailed, "newell: the crossing pair is nowhere near the work bound");
            check_order(out, "crossing quads");

            // Negative control: the unsplit pair in either order must fail the
            // oracle.
            int failed_both = 0;
            for (int flip = 0; flip < 2; ++flip) {
                std::vector<PaintPoly> naive = in;
                for (PaintPoly& p: naive) prepare_paint_poly(p, proj);
                if (flip) std::swap(naive[0], naive[1]);
                float x0 = std::min(naive[0].bb[0], naive[1].bb[0]);
                float y0 = std::min(naive[0].bb[1], naive[1].bb[1]);
                float x1 = std::max(naive[0].bb[2], naive[1].bb[2]);
                float y1 = std::max(naive[0].bb[3], naive[1].bb[3]);
                int wrong = 0;
                for (float y = y0; y <= y1; y += 1.0f)
                    for (float x = x0; x <= x1; x += 1.0f) {
                        float da = 0.0f, db = 0.0f;
                        if (!ray_hit(naive[0], x, y, da)) continue;
                        if (!ray_hit(naive[1], x, y, db)) continue;
                        if (db > da + 1e-4f) ++wrong;
                    }
                if (wrong > 0) ++failed_both;
            }
            check(failed_both == 2,
                  "newell (control): both whole-polygon orders of the crossing pair are wrong");
        }

        // ---- 2. The pinwheel: a cycle, so there is no order to sort into
        {
            // Three boards along a triangle's sides, each rising from `zlo` to
            // `zhi`: at each shared vertex board k is above board k+1, round the
            // cycle. Nothing intersects.
            std::vector<PaintPoly> in;
            constexpr double kPi = 3.14159265358979323846;
            const double cx = 0.5, cy = 0.5, r = 0.34, w = 0.105;
            const double zlo = 0.35, zhi = 0.65;
            for (int k = 0; k < 3; ++k) {
                const double a = 2.0 * kPi * k / 3.0;
                const double b = 2.0 * kPi * (k + 1) / 3.0;
                const double ax = cx + r * std::cos(a), ay = cy + r * std::sin(a);
                const double bx = cx + r * std::cos(b), by = cy + r * std::sin(b);
                double ex = bx - ax, ey = by - ay;
                const double len = std::sqrt(ex * ex + ey * ey);
                ex /= len;
                ey /= len;
                const double nx = -ey * w, ny = ex * w; // across the board
                in.push_back(quad(static_cast<std::size_t>(k),
                                  {
                                      {ax - nx, ay - ny, zlo}, {ax + nx, ay + ny, zlo},
                                      {bx + nx, by + ny, zhi}, {bx - nx, by - ny, zhi}
                                  }));
            }
            PaintOrderStats st;
            const std::vector<PaintPoly> out = paint_order(in, proj, &st);
            std::printf("  pinwheel: %zu in, %zu out, %zu splits, %zu cycles, %zu tests\n",
                        st.input, st.output, st.splits, st.cycles, st.tests);
            check(!st.bailed, "newell: the pinwheel terminates well inside the work bound");
            check_order(out, "pinwheel");
        }

        // ---- 3. Two separated quads: nothing may be split
        {
            std::vector<PaintPoly> in;
            // Close in z so they overlap on screen.
            in.push_back(quad(0, {
                                  {0.1, 0.1, 0.45}, {0.9, 0.1, 0.45},
                                  {0.9, 0.9, 0.45}, {0.1, 0.9, 0.45}
                              }));
            in.push_back(quad(1, {
                                  {0.1, 0.1, 0.55}, {0.9, 0.1, 0.55},
                                  {0.9, 0.9, 0.55}, {0.1, 0.9, 0.55}
                              }));
            PaintOrderStats st;
            const std::vector<PaintPoly> out = paint_order(in, proj, &st);
            std::printf("  stacked sheets: %zu in, %zu out, %zu splits\n",
                        st.input, st.output, st.splits);
            check(st.splits == 0 && out.size() == 2,
                  "newell: two separated quads are ordered without splitting either");
            check(out[0].object == 0 && out[1].object == 1,
                  "newell: and the lower one, which is further from this camera, comes first");
            check_order(out, "stacked sheets");
        }

        // ---- 4. A vertical plane cutting a bar grid, through the real planners.
        // Checked over what the writer emits: plan_scene3d()'s order, each entry's
        // pixel ring, and depth from its parent's plane.
        {
            constexpr int NU = 5, NV = 5;
            std::vector<double> gu(NU), gv(NV), gh(NU * NV);
            for (int i = 0; i < NU; ++i) gu[static_cast<std::size_t>(i)] = -2.0 + i;
            for (int j = 0; j < NV; ++j) gv[static_cast<std::size_t>(j)] = -2.0 + j;
            for (int i = 0; i < NU; ++i)
                for (int j = 0; j < NV; ++j)
                    gh[static_cast<std::size_t>(i * NV + j)] = 1.0 + 0.4 * ((i + j) % 3);

            FigureSnapshot fs2 = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs2.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -50.0;
            s->camera.elevation = 22.0;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            s->xmin = -3;
            s->xmax = 3;
            s->xlim_auto = false;
            s->ymin = -3;
            s->ymax = 3;
            s->ylim_auto = false;
            s->zmin = 0;
            s->zmax = 3;
            s->zlim_auto = false;

            Bar3DPlot bar;
            bar.u = gu;
            bar.v = gv;
            bar.heights = gh;
            bar.u_width = bar.v_width = 0.7;
            bar.opts.shading = 0.0f;
            s->bars3d.push_back(std::move(bar));

            // A YZ plane at x = 0 through a grid spanning x -3..3, with a heatmap.
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::YZ;
            pl.offset = 0.0;
            HeatmapPlot hp;
            hp.rows = 8;
            hp.cols = 8;
            std::vector<float> hv(64, 0.0f);
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c)
                    hv[static_cast<std::size_t>(r) * 8 + c] =
                            static_cast<float>((r + c) % 4) / 3.0f;
            hp.data = CowVec<float>(std::move(hv));
            hp.xrange = {-3.0, 3.0};
            hp.yrange = {0.0, 3.0};
            pl.sheet.heatmaps.push_back(std::move(hp));
            s->planes.push_back(std::move(pl));

            const FigureLayout lay2 = compute_figure_layout(fs2, W, H);
            const Projector3D& pj = lay2.cells[0].box3d->proj;

            const std::vector<Bar3DPolygon> bplan = plan_bars3d(pj, s->bars3d);
            const std::vector<Surface3DPolygon> splan = plan_surfaces3d(pj, s->surfaces);
            const std::vector<PlanePlanItem> pplan = plan_planes3d(pj, s->planes);
            PaintOrderStats st;
            const std::vector<ScenePaint> scene = plan_scene3d(pj, bplan, splan, pplan, {}, {}, {}, {}, &st);
            std::printf("  plane through a grid: %zu polys in, %zu out, %zu splits, %zu tests\n",
                        st.input, st.output, st.splits, st.tests);
            check(!st.bailed, "newell: the plane-through-a-grid scene stays inside the work bound");
            bool newell_here = true;
            if (const char* env0 = std::getenv("SEXTANT_NEWELL"))
                newell_here = std::atoi(env0) != 0;
            if (newell_here)
                check(st.splits > 0,
                      "newell: a plane cutting a grid forces splits -- there is no order without them");

            // Every emitted item as (pixel ring, box-space plane); a plane
            // contributes its quad once.
            struct Emitted {
                std::vector<float> xy;
                Vec3 p0, n;
                bool ok = false;
            };
            std::vector<Emitted> em;
            em.reserve(scene.size());
            for (const ScenePaint& sp: scene) {
                Emitted e;
                const std::vector<Vec3>* parent = nullptr;
                std::vector<Vec3> quad;
                if (sp.kind == ScenePaint::Kind::Bar && sp.index < bplan.size()) {
                    e.xy = sp.xy.empty() ? bplan[sp.index].xy : sp.xy;
                    parent = &bplan[sp.index].box;
                } else if (sp.kind == ScenePaint::Kind::Surface && sp.index < splan.size()) {
                    e.xy = sp.xy.empty() ? splan[sp.index].xy : sp.xy;
                    parent = &splan[sp.index].box;
                } else if (sp.kind == ScenePaint::Kind::Plane) {
                    for (const PlanePlanItem& it: pplan)
                        if (it.plane == sp.index) {
                            quad.assign(it.quad, it.quad + 4);
                            break;
                        }
                    parent = &quad;
                    if (!sp.xy.empty()) e.xy = sp.xy;
                    else {
                        // Unsplit: the plane's own quad, projected.
                        std::vector<Px3> r;
                        pj.project_polygon(quad, r);
                        for (const Px3& q: r) {
                            e.xy.push_back(q.x);
                            e.xy.push_back(q.y);
                        }
                    }
                }
                if (parent && parent->size() >= 3 && ring_plane(*parent, e.p0, e.n))
                    e.ok = true;
                em.push_back(std::move(e));
            }

            auto inside = [](const std::vector<float>& xy, float px, float py) {
                if (xy.size() < 6) return false;
                const std::size_t n = xy.size() / 2;
                int sign = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const float ex = xy[j * 2] - xy[i * 2];
                    const float ey = xy[j * 2 + 1] - xy[i * 2 + 1];
                    const float cr = ex * (py - xy[i * 2 + 1]) - ey * (px - xy[i * 2]);
                    if (std::fabs(cr) < 1e-6f) continue;
                    const int sg = cr > 0 ? 1 : -1;
                    if (sign == 0) sign = sg;
                    else if (sg != sign) return false;
                }
                return sign != 0;
            };

            const PlotRect& fr = lay2.cells[0].frame;
            int probed = 0, overlapped = 0, wrong = 0;
            for (float y = fr.y; y < fr.y + fr.h; y += 2.0f)
                for (float x = fr.x; x < fr.x + fr.w; x += 2.0f) {
                    float prev = 0.0f;
                    bool have = false, bad = false;
                    int hits = 0;
                    for (const Emitted& e: em) {
                        if (!e.ok || !inside(e.xy, x, y)) continue;
                        const Projector3D::Ray3 r = pj.ray_from_pixel(x, y);
                        const double den = dot(e.n, r.dir);
                        if (std::fabs(den) < 1e-12) continue;
                        const double t = dot(e.n, e.p0 - r.origin) / den;
                        const float d = pj.project_box(r.origin + r.dir * t).depth;
                        ++hits;
                        if (have && d > prev + 2e-3f) bad = true;
                        prev = d;
                        have = true;
                    }
                    ++probed;
                    if (hits > 1) ++overlapped;
                    if (bad) ++wrong;
                }
            std::printf("  plane through a grid: %d px probed, %d with two or more layers, "
                        "%d out of order\n", probed, overlapped, wrong);
            check(overlapped > 300, "newell: the grid and the cut really do overlap on screen");

            // SEXTANT_NEWELL=0 is the negative control: the whole-object order
            // must fail the same oracle.
            bool newell_on = true;
            if (const char* env = std::getenv("SEXTANT_NEWELL"))
                newell_on = std::atoi(env) != 0;
            if (newell_on)
                check(wrong == 0,
                      "newell: §10's reproducer emits every pixel's layers back to front, "
                      "which is the flaw that step closed");
            else
                check(st.splits == 0 && wrong > 100,
                      "newell (control): the whole-object order splits nothing and gets "
                      "§10's reproducer wrong at hundreds of pixels");
        }
    }
} // namespace lt
