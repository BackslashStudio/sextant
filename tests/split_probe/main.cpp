// sextant -- painter split probe
//
// Diagnoses scenes where the painter's split loop may not converge: calls
// paint_order() directly (no window, GL or SVG writer) and reports splits
// grouped by source polygon, which SvgSaveReport's totals can't show. Asserts
// nothing; prints tables. The painter's minimum piece area and overlap slack
// were chosen against its output.
//
// Usage: sextant_split_probe [maxN] [split budget]
#include "coord_transform3d.h"
#include "plot_objects.h"
#include "renderer/bar3d.h"
#include "renderer/figure_layout.h"
#include "renderer/painter3d.h"
#include "renderer/surface.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

using namespace sextant;
using Clock = std::chrono::steady_clock;

namespace {
    // The gallery cell's pixel size (1500x950 over a 2x3 grid); the projection
    // decides which polygons overlap.
    constexpr int W = 500, H = 475;

    // Draw every surface with its wireframe (third argument "wire").
    bool g_wire = false;

    struct Scene {
        std::vector<Bar3DPlot> bars;
        std::vector<SurfacePlot> surfaces;
    };

    // test_translucent3d's sixth cell at grid size N (otherwise as in perf_test).
    Scene make_c6(int N, bool with_bars, bool with_surface) {
        Scene sc;
        const std::size_t n = static_cast<std::size_t>(N);
        std::vector<double> gx(n), gy(n), ripple(n * n), midway(n * n);
        for (int i = 0; i < N; ++i) gx[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (N - 1);
        for (int j = 0; j < N; ++j) gy[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (N - 1);
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                const double x = gx[static_cast<std::size_t>(i)];
                const double y = gy[static_cast<std::size_t>(j)];
                const double r = std::hypot(x, y);
                const std::size_t k = static_cast<std::size_t>(i * N + j);
                ripple[k] = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
                midway[k] = -1.5 + 0.5 * ripple[k];
            }
        if (with_bars) {
            Bar3DPlot b;
            b.u = gx;
            b.v = gy;
            b.heights = ripple;
            b.opts.bottom = -1.5;
            b.u_width = 0.7 * (6.0 / (N - 1));
            b.v_width = 0.7 * (6.0 / (N - 1));
            b.opts.color = Color::Orange;
            b.opts.alpha = 0.5f;
            sc.bars.push_back(std::move(b));
        }
        if (with_surface) {
            SurfacePlot sp;
            sp.u = gx;
            sp.v = gy;
            sp.heights = midway;
            sp.opts.colormap = true;
            sp.opts.cmap = Colormap::Viridis;
            sp.opts.alpha = 0.55f;
            sp.opts.edges = g_wire;
            sc.surfaces.push_back(std::move(sp));
        }
        return sc;
    }

    FigureSnapshot snapshot_for(const Scene& sc) {
        FigureSnapshot fs;
        RenderSnapshot3D rs;
        // Auto limits, as in the gallery: they decide the box and projection.
        // The sheet sits inside the bars (`midway`).
        rs.bars3d = sc.bars;
        rs.surfaces = sc.surfaces;
        fs.axes.push_back({{1, 1, 1}, std::move(rs)});
        fs.generation = fs.data_generation = 1;
        return fs;
    }

    // plan_scene3d()'s polygon list (no planes here), built directly so the ordered
    // polygons survive for inspection.
    std::vector<PaintPoly> soup_for(const Projector3D& proj, const Scene& sc) {
        const std::vector<Bar3DPolygon> bp = plan_bars3d(proj, sc.bars);
        const std::vector<Surface3DPolygon> sp = plan_surfaces3d(proj, sc.surfaces);
        std::vector<PaintPoly> soup;
        // Two points is a stroke; plan_scene3d() orders those too.
        for (std::size_t i = 0; i < bp.size(); ++i) {
            if (bp[i].box.size() < 2) continue;
            PaintPoly p;
            p.ring = bp[i].box;
            p.kind = PaintPoly::Kind::Bar;
            p.object = bp[i].plot;
            p.element = bp[i].bar;
            p.rank = i;
            p.source = i;
            soup.push_back(std::move(p));
        }
        for (std::size_t i = 0; i < sp.size(); ++i) {
            if (sp[i].box.size() < 2) continue;
            PaintPoly p;
            p.ring = sp[i].box;
            p.kind = PaintPoly::Kind::Surface;
            p.object = sp[i].plot;
            p.element = sp[i].cell;
            p.rank = i;
            p.source = i;
            soup.push_back(std::move(p));
        }
        return soup;
    }

    struct Run {
        PaintOrderStats st;
        double ms = 0.0;
        std::size_t cut_sources = 0; // distinct polygons cut at least once
        std::size_t worst_pieces = 0; // and the most any one of them came out as
        char worst_kind = '-';
        std::size_t worst_source = 0;
    };

    Run run_scene(const Scene& sc, std::size_t budget, std::size_t work) {
        const FigureSnapshot fs = snapshot_for(sc);
        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;

        std::vector<PaintPoly> soup = soup_for(proj, sc);
        Run r;
        const auto t0 = Clock::now();
        const std::vector<PaintPoly> ordered =
                paint_order(std::move(soup), proj, &r.st, work, budget);
        r.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        std::map<std::pair<int, std::size_t>, std::size_t> pieces;
        for (const PaintPoly& p: ordered) ++pieces[{static_cast<int>(p.kind), p.source}];
        for (const auto& entry: pieces) {
            if (entry.second < 2) continue;
            ++r.cut_sources;
            if (entry.second > r.worst_pieces) {
                r.worst_pieces = entry.second;
                r.worst_kind = entry.first.first == static_cast<int>(PaintPoly::Kind::Bar)
                                   ? 'B'
                                   : 'S';
                r.worst_source = entry.first.second;
            }
        }
        return r;
    }

    // Box-space area of a ring, via the same summed cross terms ring_plane() uses.
    double ring_area(const std::vector<Vec3>& ring) {
        if (ring.size() < 3) return 0.0;
        Vec3 sum{0.0, 0.0, 0.0};
        const std::size_t m = ring.size();
        for (std::size_t i = 0; i < m; ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1) % m];
            sum.x += (a.y - b.y) * (a.z + b.z);
            sum.y += (a.z - b.z) * (a.x + b.x);
            sum.z += (a.x - b.x) * (a.y + b.y);
        }
        return 0.5 * std::sqrt(dot(sum, sum));
    }

    // Piece areas of the worst-cut polygons: a large scene gives comparable sizes;
    // a shaving loop gives a long tail of near-zero areas.
    void detail(int N, std::size_t budget) {
        const Scene sc = make_c6(N, true, true);
        const FigureSnapshot fs = snapshot_for(sc);
        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;

        PaintOrderStats st;
        const std::vector<PaintPoly> ordered =
                paint_order(soup_for(proj, sc), proj, &st, 2000000000ull, budget);

        std::map<std::pair<int, std::size_t>, std::vector<double>> areas;
        for (const PaintPoly& p: ordered)
            areas[{static_cast<int>(p.kind), p.source}].push_back(ring_area(p.ring));

        std::vector<std::pair<std::size_t, std::pair<int, std::size_t>>> rank;
        for (const auto& e: areas) rank.push_back({e.second.size(), e.first});
        std::sort(rank.rbegin(), rank.rend());

        std::printf("\nN=%d, budget %zu -> %zu splits, %zu unresolved. "
                    "The most-cut polygons:\n", N, budget, st.splits, st.unresolved);
        std::printf("  %-10s %8s %12s %12s %12s %10s\n",
                    "polygon", "pieces", "area sum", "area max", "area min", "< 1e-9");
        for (std::size_t i = 0; i < rank.size() && i < 5; ++i) {
            std::vector<double>& a = areas[rank[i].second];
            std::sort(a.begin(), a.end());
            double sum = 0.0;
            std::size_t tiny = 0;
            for (double v: a) {
                sum += v;
                if (v < 1e-9) ++tiny;
            }
            char name[32];
            std::snprintf(name, sizeof name, "%c%zu",
                          rank[i].second.first == static_cast<int>(PaintPoly::Kind::Bar)
                              ? 'B'
                              : 'S',
                          rank[i].second.second);
            std::printf("  %-10s %8zu %12.3e %12.3e %12.3e %10zu\n",
                        name, a.size(), sum, a.back(), a.front(), tiny);
        }
    }

    // Newell's five tests, re-implemented (painter3d.cpp's are file-local), to tell
    // whether a misordered pair was wrongly separated by a test or never compared.
    struct Ladder {
        bool depth, bbox, behind, in_front, overlap;
    };

    Ladder ladder(const PaintPoly& P, const PaintPoly& Q, const Projector3D& proj) {
        Ladder L{};
        L.depth = Q.dmax <= P.dmin;
        L.bbox = P.bb[2] < Q.bb[0] + 0.5f || Q.bb[2] < P.bb[0] + 0.5f
                 || P.bb[3] < Q.bb[1] + 0.5f || Q.bb[3] < P.bb[1] + 0.5f;
        auto oriented = [&](const std::vector<Vec3>& ring, Vec3& p0, Vec3& n) {
            if (!ring_plane(ring, p0, n)) return false;
            if (!proj.faces_camera(p0, n)) n = n * -1.0;
            return true;
        };
        Vec3 qp0, qn, pp0, pn;
        if (oriented(Q.ring, qp0, qn)) {
            L.behind = true;
            for (const Vec3& v: P.ring)
                if (dot(v - qp0, qn) > 1e-7) {
                    L.behind = false;
                    break;
                }
        }
        if (oriented(P.ring, pp0, pn)) {
            L.in_front = true;
            for (const Vec3& v: Q.ring)
                if (dot(v - pp0, pn) < -1e-7) {
                    L.in_front = false;
                    break;
                }
        }
        // Separating-axis on the projections, with painter3d.cpp's slack.
        auto separate = [](const std::vector<float>& s, const std::vector<float>& o) {
            const std::size_t n = s.size() / 2;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                const float ex = s[j * 2] - s[i * 2], ey = s[j * 2 + 1] - s[i * 2 + 1];
                const float len = std::sqrt(ex * ex + ey * ey);
                if (len < 1e-6f) continue;
                const float nx = -ey / len, ny = ex / len;
                float a0 = 1e30f, a1 = -1e30f, b0 = 1e30f, b1 = -1e30f;
                for (std::size_t k = 0; k < n; ++k) {
                    const float d = nx * s[k * 2] + ny * s[k * 2 + 1];
                    a0 = std::min(a0, d);
                    a1 = std::max(a1, d);
                }
                for (std::size_t k = 0; k * 2 + 1 < o.size(); ++k) {
                    const float d = nx * o[k * 2] + ny * o[k * 2 + 1];
                    b0 = std::min(b0, d);
                    b1 = std::max(b1, d);
                }
                if (a1 < b0 + 0.15f || b1 < a0 + 0.15f) return true;
            }
            return false;
        };
        // A stroke against a polygon is tested; two strokes never are.
        L.overlap = P.px.size() >= 4 && Q.px.size() >= 4
                    && (P.px.size() >= 6 || Q.px.size() >= 6)
                    && !separate(P.px, Q.px) && !separate(Q.px, P.px);
        return L;
    }

    // test_scene3d_svg_order()'s ray-cast oracle, run here to describe the pixels
    // it flags: intersect each emitted polygon's plane at the pixel and check the
    // depths are non-increasing.
    void wrong_pixels(int N, int w, int h) {
        const Scene sc = make_c6(N, true, true);
        FigureSnapshot fs = snapshot_for(sc);
        const FigureLayout lay = compute_figure_layout(fs, w, h);
        const Projector3D& pj = lay.cells[0].box3d->proj;

        PaintOrderStats st;
        const std::vector<PaintPoly> em =
                paint_order(soup_for(pj, sc), pj, &st, 2000000000ull, 4000000ull);

        auto inside = [](const PaintPoly& e, float px, float py) {
            if (px < e.bb[0] || px > e.bb[2] || py < e.bb[1] || py > e.bb[3]) return false;
            const std::size_t n = e.px.size() / 2;
            if (n < 3) return false;
            int sign = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                const float ex = e.px[j * 2] - e.px[i * 2];
                const float ey = e.px[j * 2 + 1] - e.px[i * 2 + 1];
                const float cr = ex * (py - e.px[i * 2 + 1]) - ey * (px - e.px[i * 2]);
                if (std::fabs(cr) < 1e-6f) continue;
                const int sg = cr > 0 ? 1 : -1;
                if (sign == 0) sign = sg;
                else if (sg != sign) return false;
            }
            return sign != 0;
        };

        const PlotRect& fr = lay.cells[0].frame;
        int reported = 0, wrong = 0;
        std::printf("\nout-of-order pixels at N=%d, %dx%d, default camera "
                    "(%zu polys -> %zu, %zu splits)\n", N, w, h, st.input, st.output,
                    st.splits);
        for (float y = fr.y; y < fr.y + fr.h; y += 3.0f)
            for (float x = fr.x; x < fr.x + fr.w; x += 3.0f) {
                struct Hit {
                    const PaintPoly* p;
                    float d;
                };
                std::vector<Hit> stack;
                for (const PaintPoly& e: em) {
                    if (!inside(e, x, y)) continue;
                    Vec3 p0, n;
                    if (!ring_plane(e.ring, p0, n)) continue;
                    const Projector3D::Ray3 r = pj.ray_from_pixel(x, y);
                    const double den = dot(n, r.dir);
                    if (std::fabs(den) < 1e-12) continue;
                    const double t = dot(n, p0 - r.origin) / den;
                    stack.push_back({&e, pj.project_box(r.origin + r.dir * t).depth});
                }
                bool bad = false;
                for (std::size_t i = 1; i < stack.size(); ++i)
                    if (stack[i].d > stack[i - 1].d + 3e-3f) bad = true;
                if (!bad) continue;
                ++wrong;
                if (reported++ >= 4) continue;
                std::printf("  (%.0f,%.0f) %zu layers:\n", x, y, stack.size());
                for (std::size_t i = 0; i < stack.size(); ++i) {
                    const PaintPoly& p = *stack[i].p;
                    std::printf("    %c%-4zu el%-5zu %s depth %9.5f  area %10.3e  ring %zu%s\n",
                                p.kind == PaintPoly::Kind::Bar ? 'B' : 'S', p.source,
                                p.element, p.split ? "piece" : "whole ", stack[i].d,
                                ring_area(p.ring), p.ring.size(),
                                (i && stack[i].d > stack[i - 1].d + 3e-3f)
                                    ? "   <-- OUT OF ORDER"
                                    : "");
                    if (!i || stack[i].d <= stack[i - 1].d + 3e-3f) continue;
                    // P was emitted first (believed farther); report which of the
                    // five tests said so.
                    const Ladder L = ladder(*stack[i - 1].p, p, pj);
                    std::printf("       ladder P=earlier Q=later: depth=%d bbox=%d "
                                "P-behind-Q=%d Q-infront-P=%d projections-overlap=%d\n",
                                L.depth, L.bbox, L.behind, L.in_front, L.overlap);
                    for (const PaintPoly* e: {stack[i - 1].p, &p}) {
                        std::printf("       px:");
                        for (std::size_t k = 0; k + 1 < e->px.size(); k += 2)
                            std::printf(" (%.3f,%.3f)", e->px[k], e->px[k + 1]);
                        std::printf("\n");
                    }
                }
            }
        std::printf("  %d out of order\n", wrong);
    }
} // namespace

int main(int argc, char** argv) {
    const int maxN = argc > 1 ? std::atoi(argv[1]) : 13;
    const std::size_t budget =
            argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 4000000ull;
    g_wire = argc > 3 && std::string_view(argv[3]) == "wire";
    constexpr std::size_t kWork = 2000000000ull;

    std::printf("c6: a sheet at -1.5 + 0.5*ripple through a grid of translucent\n"
                "bars from -1.5 to ripple, %dx%d px, default camera%s.\n", W, H,
                g_wire ? ", surface wireframe on" : "");
    std::printf("split budget %zu, work budget %zu\n\n", budget, kWork);
    std::printf("%3s %7s %9s %9s %12s %9s %7s %7s %7s\n",
                "N", "polys", "splits", "cycles", "tests", "ms", "cut", "worst",
                "unres");
    for (int N = 3; N <= maxN; ++N) {
        const Scene sc = make_c6(N, true, true);
        const Run r = run_scene(sc, budget, kWork);
        std::printf("%3d %7zu %9zu %9zu %12zu %9.0f %7zu %6zu%c %7zu %s\n",
                    N, r.st.input, r.st.splits, r.st.cycles, r.st.tests, r.ms,
                    r.cut_sources, r.worst_pieces, r.worst_kind, r.st.unresolved,
                    r.st.bailed
                        ? (r.st.bailed_on_splits
                               ? "BAILED(splits)"
                               : "BAILED(work)")
                        : "");
        std::fflush(stdout);
        if (r.st.bailed) {
            detail(N, 3000);
            std::fflush(stdout);
            break;
        }
    }

    // Which pixels are wrong at test_scene3d_svg_order()'s size and camera.
    wrong_pixels(13, 460, 420);

    // A smooth sheet alone occludes itself nowhere, so it must never split:
    // the control for how tight the overlap slack may be.
    {
        constexpr int M = 50;
        Scene sc;
        SurfacePlot sp;
        std::vector<double> gu(M), gv(M), h(M * M);
        for (int i = 0; i < M; ++i) gu[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (M - 1);
        for (int j = 0; j < M; ++j) gv[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (M - 1);
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < M; ++j)
                h[static_cast<std::size_t>(i * M + j)] =
                        0.6 * std::sin(gu[static_cast<std::size_t>(i)])
                        * std::cos(gv[static_cast<std::size_t>(j)]);
        sp.u = gu;
        sp.v = gv;
        sp.heights = h;
        sp.opts.colormap = true;
        sp.opts.edges = g_wire;
        sc.surfaces.push_back(std::move(sp));
        const Run r = run_scene(sc, budget, kWork);
        std::printf("\nsmooth 50x50 sheet alone (must not split): "
                    "%zu polys, %zu splits, %zu tests\n",
                    r.st.input, r.st.splits, r.st.tests);
    }

    // Controls: neither object interleaves with itself, so any split here is
    // spurious.
    std::printf("\ncontrols at N=%d, each object alone\n", maxN);
    for (int which = 0; which < 2; ++which) {
        const Scene sc = make_c6(maxN, which == 0, which == 1);
        const Run r = run_scene(sc, budget, kWork);
        std::printf("  %-8s %6zu polys %8zu splits %11zu tests %8.0f ms\n",
                    which == 0 ? "bars" : "surface",
                    r.st.input, r.st.splits, r.st.tests, r.ms);
        std::fflush(stdout);
    }
    return 0;
}
