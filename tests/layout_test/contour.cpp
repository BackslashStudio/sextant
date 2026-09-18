// Marching squares, the pixel plan both render paths share, and the cache.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// ===========================================================================
// Contour tracing
// ===========================================================================
//
// trace_contours() and plan_contours() are what both render paths call, and
// neither is reachable through <sextant/sextant.h>. Checking them here rather
// than by eye means a wrong answer is a number: the two failures this guards
// against -- a level traced against the wrong row for origin="upper", and a
// saddle cell joined the wrong way -- both produce plausible pictures.

// A frame that maps the 5x5 grid's [0,5]x[0,5] data space onto 500x500
// pixels, so one data unit is exactly 100 px and every expected coordinate
// below is a round number.
sextant::CoordTransform unit_tr() {
    return sextant::CoordTransform{ 0.0, 5.0, 0.0, 5.0,
                                    0.0f, 0.0f, 500.0f, 500.0f, 500.0f, 500.0f };
}

void test_contour_tracing() {
    std::printf("\n[contour tracing]\n");

    // --- A ramp along x. The level-2 iso-line has one place it can be: the
    // column of sample centres whose value is exactly 2, i.e. x = 2.5,
    // running the full height of the sample grid.
    {
        sextant::HeatmapOptions o;
        o.contours = { 2.0 };
        const auto hp = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);
        const auto set = sextant::trace_contours(hp);

        check(set.size() == 1, "ramp: one level traces one line");
        if (set.size() == 1) {
            const auto& l = set[0];
            check(l.level == 2.0, "  and it carries its level");
            check(!l.closed, "  and it is open (it runs off the grid)");
            check(l.x.size() == 5, "  with one vertex per sample row");
            bool all_x = true, y_ok = true;
            for (std::size_t i = 0; i < l.x.size(); ++i) {
                if (l.x[i] != 2.5f) all_x = false;
                if (std::fabs(l.y[i] - (0.5f + static_cast<float>(i))) > 1e-6f) y_ok = false;
            }
            check(all_x, "  every vertex at x=2.5 (the sample centre, not a cell edge)");
            check(y_ok, "  spanning y=0.5..4.5 in order");
        }
    }

    // --- Origin. Storage is row-major with row 0 first either way; only
    // which *data* row a sample reads changes. Trace the same buffer both
    // ways and the line has to move — if it does not, the trace is ignoring
    // origin and will sit mirrored on top of the image for "upper".
    {
        auto ramp_rows = [](int r, int) { return float(r); };
        sextant::HeatmapOptions lo;  lo.contours = { 1.0 };  lo.origin = "lower";
        sextant::HeatmapOptions up;  up.contours = { 1.0 };  up.origin = "upper";
        const auto low_set = sextant::trace_contours(make_heatmap(5, 5, ramp_rows, lo));
        const auto up_set  = sextant::trace_contours(make_heatmap(5, 5, ramp_rows, up));

        check(low_set.size() == 1 && up_set.size() == 1, "origin: both trace one line");
        if (low_set.size() == 1 && up_set.size() == 1) {
            check(std::fabs(low_set[0].y[0] - 1.5f) < 1e-6f,
                  "  origin=lower puts level 1 at y=1.5");
            check(std::fabs(up_set[0].y[0] - 3.5f) < 1e-6f,
                  "  origin=upper puts the same level at y=3.5 (row 1 counted from the top)");
        }
    }

    // --- A single hot cell. The contour around it must close, and close on
    // the cell it surrounds — a chaining bug typically yields two open halves
    // that still draw as a diamond and look right.
    {
        sextant::HeatmapOptions o;
        o.contours = { 5.0 };
        const auto hp = make_heatmap(5, 5,
            [](int r, int c) { return (r == 2 && c == 2) ? 10.0f : 0.0f; }, o);
        const auto set = sextant::trace_contours(hp);

        check(set.size() == 1, "island: one closed line around the hot cell");
        if (set.size() == 1) {
            const auto& l = set[0];
            check(l.closed, "  reported closed");
            check(l.x.size() == 5, "  four crossings, the first repeated to close it");
            check(l.x.front() == l.x.back() && l.y.front() == l.y.back(),
                  "  and the repeat really is the same point");
            float cx = 0.0f, cy = 0.0f;
            for (std::size_t i = 0; i + 1 < l.x.size(); ++i) { cx += l.x[i]; cy += l.y[i]; }
            cx /= 4.0f; cy /= 4.0f;
            check(std::fabs(cx - 2.5f) < 1e-5f && std::fabs(cy - 2.5f) < 1e-5f,
                  "  centred on the cell it encloses");
        }
    }

    // --- Levels, and the two ways of asking for nothing.
    {
        sextant::HeatmapOptions o;
        o.contours = { 1.0, 2.0, 3.0 };
        const auto set = sextant::trace_contours(
            make_heatmap(5, 5, [](int, int c) { return float(c); }, o));
        check(set.size() == 3, "three levels trace three lines");
        check(set[0].level == 1.0 && set[1].level == 2.0 && set[2].level == 3.0,
              "  in the order the levels were given");

        sextant::HeatmapOptions out_of_range;
        out_of_range.contours = { 99.0 };
        check(sextant::trace_contours(
                  make_heatmap(5, 5, [](int, int c) { return float(c); }, out_of_range)).empty(),
              "a level outside the data traces nothing");

        sextant::HeatmapOptions thin;
        thin.contours = { 0.5 };
        check(sextant::trace_contours(
                  make_heatmap(1, 5, [](int, int c) { return float(c); }, thin)).empty(),
              "a one-row heatmap traces nothing (marching squares needs four samples)");
    }
}

void test_contour_planning() {
    std::printf("\n[contour planning]\n");

    sextant::HeatmapOptions o;
    o.contours = { 2.0 };
    const auto hp  = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);
    const auto set = sextant::trace_contours(hp);
    const auto tr  = unit_tr();

    // Unlabelled: the line is projected and handed over whole.
    {
        const auto d = sextant::plan_contours(set, tr, o, "");
        check(d.runs.size() == 1 && d.labels.empty(),
              "no labels: one unbroken run, no text");
        if (d.runs.size() == 1) {
            check(d.runs[0].px.size() == 5, "  every vertex kept");
            check(std::fabs(d.runs[0].px[0] - 250.0f) < 1e-3f &&
                  std::fabs(d.runs[0].py[0] - 450.0f) < 1e-3f,
                  "  projected through the transform (x=2.5 -> 250 px, y=0.5 -> 450 px)");
        }
    }

    // Labelled: the line is broken around the text, and the hole is exactly
    // as wide as the text plus its padding. This is the assertion that the
    // gap is measured against the font rather than being a fixed guess — a
    // hardcoded gap would not track the font size below.
    {
        sextant::HeatmapOptions lo = o;
        lo.contour_labels = true;
        const auto d = sextant::plan_contours(set, tr, lo, "");

        check(d.runs.size() == 2 && d.labels.size() == 1,
              "labels on: the line is broken in two around one label");
        if (d.runs.size() == 2 && d.labels.size() == 1) {
            check(d.labels[0].text == "2", "  labelled with the level, %g-formatted");

            const float gap_top    = d.runs[0].py.back();
            const float gap_bottom = d.runs[1].py.front();
            const float hole = std::fabs(gap_top - gap_bottom);
            const float want = sextant::text_width("", lo.contour_fontsize, "2") + 6.0f;
            check(std::fabs(hole - want) < 0.05f,
                  "  the hole is the text width plus 2x3px of padding");

            check(std::fabs(d.labels[0].x - 250.0f) < 1e-3f &&
                  std::fabs(d.labels[0].y - 250.0f) < 1e-3f,
                  "  the label sits at the line's arc-length midpoint");
            check(std::fabs(d.labels[0].angle + 1.5707963f) < 1e-4f,
                  "  a vertical line labels bottom-to-top (-90 degrees), not upside down");
        }

        // Same line, bigger text: the hole has to grow with it.
        sextant::HeatmapOptions big = lo;
        big.contour_fontsize = 30.0f;
        const auto d2 = sextant::plan_contours(set, tr, big, "");
        if (d2.runs.size() == 2 && d.runs.size() == 2) {
            const float hole1 = std::fabs(d.runs[0].py.back()  - d.runs[1].py.front());
            const float hole2 = std::fabs(d2.runs[0].py.back() - d2.runs[1].py.front());
            check(hole2 > hole1 + 5.0f, "  and a larger font cuts a wider hole");
        }
    }

    // A horizontal line labels horizontally.
    {
        sextant::HeatmapOptions ho;
        ho.contours = { 2.0 };
        ho.contour_labels = true;
        const auto hset = sextant::trace_contours(
            make_heatmap(5, 5, [](int r, int) { return float(r); }, ho));
        const auto d = sextant::plan_contours(hset, tr, ho, "");
        check(d.labels.size() == 1 && std::fabs(d.labels[0].angle) < 1e-4f,
              "a horizontal line's label is not rotated");
    }

    // Too short to break: label dropped, line drawn whole. Squeeze the same
    // line into 20 px of frame so it cannot host its own text.
    {
        sextant::HeatmapOptions lo = o;
        lo.contour_labels = true;
        sextant::CoordTransform tiny = tr;
        tiny.ph = 20.0f; tiny.pw = 20.0f;
        const auto d = sextant::plan_contours(set, tiny, lo, "");
        check(d.runs.size() == 1 && d.labels.empty(),
              "a line too short for its label keeps the line and drops the label");
    }

    // Panned right off the frame: nothing to draw at all.
    {
        sextant::CoordTransform away = tr;
        away.xmin = 100.0; away.xmax = 105.0;
        const auto d = sextant::plan_contours(set, away, o, "");
        check(d.runs.empty(), "a line outside the frame is rejected before projection");
    }
}

void test_contour_cache() {
    std::printf("\n[contour cache]\n");

    sextant::HeatmapOptions o;
    o.contours = { 2.0 };
    auto hp = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);

    sextant::ContourCache cache;
    check(cache.get(0, -1, 0, 7, hp).size() == 1, "first call traces");

    // Flatten the data behind the cache's back, keeping the generation. A
    // cache that re-traced here would report 0 lines; holding the old answer
    // is the whole point — a pan republishes the snapshot every frame without
    // touching the data, and re-running marching squares on each of those
    // frames is what this exists to avoid.
    hp.data.mut().assign(25, 0.0f);
    check(cache.get(0, -1, 0, 7, hp).size() == 1,
          "same data generation reuses the trace, even though the data changed");
    check(cache.get(0, -1, 0, 8, hp).empty(),
          "a new data generation re-traces and sees the flat field");

    // The level list is part of the key: changing it must re-trace even
    // though the data (and its generation) did not move.
    auto hp2 = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);
    check(cache.get(1, -1, 0, 9, hp2).size() == 1, "second plot slot traces on its own");
    hp2.opts.contours = { 1.0, 2.0, 3.0 };
    check(cache.get(1, -1, 0, 9, hp2).size() == 3,
          "changing the levels re-traces at the same generation");

    // Generation 0 means "unstamped", which must never be treated as a hit.
    sextant::ContourCache fresh;
    check(fresh.get(0, -1, 0, 0, hp2).size() == 3, "generation 0 traces");
    hp2.opts.contours = { 2.0 };
    check(fresh.get(0, -1, 0, 0, hp2).size() == 1, "generation 0 never caches");

    // What the cache is worth, on a grid big enough for the trace to matter.
    // The number is the point: this is per level per frame otherwise, on
    // every frame of a drag, on the render thread.
    {
        const int n = 512;
        sextant::HeatmapOptions o;
        o.contours = { 0.2, 0.4, 0.6, 0.8, 1.0, 1.2, 1.4, 1.6 };
        auto big = make_heatmap(n, n, [n](int r, int c) {
            const float dx = (c - n / 2) / (n / 4.0f), dy = (r - n / 2) / (n / 4.0f);
            return std::sqrt(dx * dx + dy * dy);
        }, o);

        sextant::ContourCache cc;
        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t lines = cc.get(0, -1, 0, 1, big).size();
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) cc.get(0, -1, 0, 1, big);
        const auto t2 = std::chrono::steady_clock::now();

        const double trace_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double hit_ms   = std::chrono::duration<double, std::milli>(t2 - t1).count() / 100.0;
        std::printf("  512x512, 8 levels: trace %.2f ms, cached lookup %.4f ms (%zu lines)\n",
                    trace_ms, hit_ms, lines);
        check(lines >= 8, "  the big field traces at least one line per level");
        check(hit_ms * 20.0 < trace_ms,
              "  and a cache hit is at least 20x cheaper than re-tracing");
    }
}

}  // namespace lt
