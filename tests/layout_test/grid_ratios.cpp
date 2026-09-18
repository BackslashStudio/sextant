// Grid weights: sharing the grid by weight, the inverse, the setters, the
// journal, and the boundary drag. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"

namespace lt {
    namespace {
        using namespace sextant;

        constexpr int GW = 900, GH = 600;

        FigureSnapshot grid_snapshot(int rows, int cols) {
            FigureSnapshot fs = make_snapshot(rows, cols, rows * cols);
            for (auto& fa: fs.axes) {
                RenderSnapshot& s = *fa.snap2d();
                s.title = "T";
                s.xtitle = "x";
                s.ytitle = "y";
            }
            fs.col_gap = 20.0f;
            fs.row_gap = 16.0f;
            fs.generation = fs.data_generation = fs.layout_generation = 1;
            return fs;
        }

        bool same_cells(const FigureLayout& a, const FigureLayout& b, float tol = 1e-2f) {
            if (a.cells.size() != b.cells.size()) return false;
            for (std::size_t i = 0; i < a.cells.size(); ++i) {
                const PlotRect& p = a.cells[i].cell;
                const PlotRect& q = b.cells[i].cell;
                const PlotRect& f = a.cells[i].frame;
                const PlotRect& g = b.cells[i].frame;
                if (!near_px(p.x, q.x, tol) || !near_px(p.w, q.w, tol) || !near_px(p.y, q.y, tol)
                    || !near_px(p.h, q.h, tol) || !near_px(f.w, g.w, tol) || !near_px(f.h, g.h, tol))
                    return false;
            }
            return true;
        }
    } // namespace

    // Weights share out the space left after the margins and gaps; a span takes
    // the sum of its weights plus the gaps inside it.
    void test_grid_ratio_layout() {
        std::printf("\n[grid ratios: layout]\n");

        FigureSnapshot fs = grid_snapshot(1, 2);
        fs.col_ratios = {2.0f, 1.0f};
        const FigureLayout l = compute_figure_layout(fs, GW, GH);
        const PlotRect& a = l.cells[0].cell;
        const PlotRect& b = l.cells[1].cell;
        check(near_px(a.w, 2.0f * b.w, 1e-2f), "ratios: {2, 1} makes the left cell twice the right");
        check(near_px(b.x, a.x + a.w + fs.col_gap, 1e-2f)
              && near_px(b.x + b.w, GW - fs.margins.right, 1e-2f),
              "ratios: and the two still fill the width between the margins, one gap apart");

        FigureSnapshot eq = grid_snapshot(1, 2);
        const FigureLayout le = compute_figure_layout(eq, GW, GH);
        eq.col_ratios = {1.0f, 1.0f};
        const FigureLayout l11 = compute_figure_layout(eq, GW, GH);
        eq.col_ratios = {3.5f, 3.5f};
        const FigureLayout l35 = compute_figure_layout(eq, GW, GH);
        check(same_cells(le, l11) && same_cells(le, l35),
              "ratios: equal weights, of any size, are the default layout");

        // Rows too, and a span: a 2x3 grid with the top row spanning columns 1-2.
        FigureSnapshot sp = grid_snapshot(2, 3);
        sp.axes[1].slot = AxesSlot{2, 3, 1, 2}; // cells 1..2
        sp.axes.erase(sp.axes.begin()); // drop the old cell 1
        sp.col_ratios = {1.0f, 2.0f, 3.0f};
        sp.row_ratios = {3.0f, 1.0f};
        const FigureLayout ls = compute_figure_layout(sp, GW, GH);
        const GridTracks t = grid_tracks(sp, ls.suptitle_band, GW, GH);
        const PlotRect& span = ls.cells[0].cell;
        check(near_px(span.w, t.col_w[0] + t.col_w[1] + sp.col_gap, 1e-2f)
              && near_px(t.col_w[1], 2.0f * t.col_w[0], 1e-2f)
              && near_px(t.col_w[2], 3.0f * t.col_w[0], 1e-2f),
              "ratios: a span is its tracks' weighted widths plus the gap between them");
        check(near_px(t.row_h[0], 3.0f * t.row_h[1], 1e-2f)
              && near_px(ls.cells.back().cell.h, t.row_h[1], 1e-2f),
              "ratios: row weights share out the height the same way");

        // Weights that do not fit the grid fall back to equal rather than crash.
        FigureSnapshot bad = grid_snapshot(1, 2);
        bad.col_ratios = {1.0f, 2.0f, 3.0f};
        const FigureLayout lb = compute_figure_layout(bad, GW, GH);
        bad.col_ratios = {1.0f, -1.0f};
        const FigureLayout ln = compute_figure_layout(bad, GW, GH);
        check(same_cells(lb, le) && same_cells(ln, le),
              "ratios: a count that does not match the grid, or a negative weight, is equal weights");

        // Weights are not measured, so a stored measure lays out any of them.
        FigureSnapshot st = grid_snapshot(1, 2);
        const FigureMeasure m = measure_figure(st);
        st.col_ratios = {1.0f, 4.0f};
        check(same_cells(compute_figure_layout(st, m, GW, GH), compute_figure_layout(st, GW, GH)),
              "ratios: a stored measure taken before the weights changed lays them out exactly");

        // Weights are a submission, not navigation.
        FigureEdits e;
        e.col_ratios = std::vector<float>{1.0f, 2.0f};
        check(!is_navigation_only(e), "ratios: a weight edit refits the stored layout");
    }

    // The inverse under weights: the figure size at which a slot's frame comes out
    // as asked, including a span's.
    void test_grid_ratio_inverse() {
        std::printf("\n[grid ratios: the inverse]\n");

        FigureSnapshot fs = grid_snapshot(2, 3);
        fs.axes[1].slot = AxesSlot{2, 3, 1, 2};
        fs.axes.erase(fs.axes.begin());
        fs.col_ratios = {1.0f, 2.5f, 0.7f};
        fs.row_ratios = {2.0f, 1.0f};

        int trips = 0, misses = 0;
        for (const auto& fa: fs.axes)
            for (const auto& [fw, fh]: {std::pair{180, 120}, std::pair{400, 260}}) {
                const LayoutSize sz = figure_size_for_frame(fs, fa.slot.index, float(fw), float(fh));
                const FigureLayout lay = compute_figure_layout(fs, int(std::lround(sz.width)),
                                                               int(std::lround(sz.height)));
                for (const auto& c: lay.cells) {
                    if (c.slot.index != fa.slot.index) continue;
                    ++trips;
                    if (std::fabs(c.frame.w - fw) > 1.0f || std::fabs(c.frame.h - fh) > 1.0f) {
                        ++misses;
                        std::printf("    slot %d want %dx%d got %.1fx%.1f\n", fa.slot.index, fw, fh,
                                    c.frame.w, c.frame.h);
                    }
                }
            }
        check(trips == 10 && misses == 0,
              "ratios: the inverse round-trips every slot, the span included (" + std::to_string(trips)
              + " trips)");
    }

    // The public setters: what they refuse, and that a saved figure honours them.
    void test_grid_ratio_api() {
        std::printf("\n[grid ratios: public API]\n");

        auto throws = [](auto&& f) {
            try { f(); } catch (const std::invalid_argument&) { return true; }
            return false;
        }; {
            auto fig = Figure::create({.width = 800, .height = 400});
            check(throws([&] { fig->set_col_ratios({1.0f, 0.0f}); })
                  && throws([&] { fig->set_col_ratios({std::nanf(""), 1.0f}); })
                  && throws([&] { fig->set_row_ratios({-2.0f}); }),
                  "api: a weight that is zero, negative or not finite is refused");
            fig->set_col_ratios({3.0f, 1.0f});
            check(fig->col_ratios() == std::vector<float>{3.0f, 1.0f},
                  "api: with no grid yet any count is held, and read back");
            check(throws([&] { fig->add_subplot(1, 3, 1); }),
                  "api: and the call that fixes a grid of another shape refuses it");
            check(!throws([&] { fig->add_subplot(1, 2, 1); }), "api: a matching shape is accepted");
            check(throws([&] { fig->set_col_ratios({1.0f, 1.0f, 1.0f}); })
                  && throws([&] { fig->set_row_ratios({1.0f, 1.0f}); }),
                  "api: once the grid has a shape, a count that does not match it is refused");
            fig->set_col_ratios({});
            check(fig->col_ratios().empty(), "api: empty goes back to equal weights");
        } {
            auto fig = Figure::create({.width = 800, .height = 400});
            fig->set_col_ratios({1.0f, 1.0f});
            check(throws([&] { fig->axes(); }),
                  "api: an implicit 1x1 axes refuses two column weights too");
        }

        // A saved figure: the wide column's frame is the wider one.
        std::vector<double> x{0.0, 1.0, 2.0}, y{0.0, 1.0, 4.0};
        PlotRect wide{}, narrow{};
        for (const auto& [ratios, path, out]:
             {
                 std::tuple{std::vector<float>{3.0f, 1.0f}, "ratios_wide.svg", &wide},
                 std::tuple{std::vector<float>{1.0f, 3.0f}, "ratios_narrow.svg", &narrow}
             }) {
            auto fig = Figure::create({.width = 800, .height = 400});
            fig->add_subplot(1, 2, 1)->line(x, y);
            fig->add_subplot(1, 2, 2)->line(x, y);
            fig->set_col_ratios(ratios);
            fig->savefig(path);
            check(read_first_svg_frame(path, *out), std::string("api: ") + path + " written and parsed");
        }
        check(wide.w > 2.5f * narrow.w, "api: savefig() lays the grid out by its weights");

        // size_for_frame() / resize_to_frame() under weights, on the narrow slot.
        auto fig = Figure::create({.width = 800, .height = 400});
        fig->add_subplot(1, 2, 1)->line(x, y);
        fig->add_subplot(1, 2, 2)->line(x, y);
        fig->set_col_ratios({3.0f, 1.0f});
        fig->resize_to_frame(150, 200, 2);
        fig->savefig("ratios_resized.svg");
        std::ifstream f("ratios_resized.svg");
        const std::string svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // The second frame's white background rect.
        std::size_t at = svg.find("\" fill=\"white\"/>");
        at = at == std::string::npos ? at : svg.find("\" fill=\"white\"/>", at + 1);
        PlotRect r{};
        bool parsed = false;
        if (at != std::string::npos) {
            const std::size_t start = svg.rfind("<rect x=\"", at);
            parsed = start != std::string::npos
                     && std::sscanf(svg.c_str() + start, "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                                    &r.x, &r.y, &r.w, &r.h) == 4;
        }
        check(parsed && std::fabs(r.w - 150.0f) <= 1.0f && std::fabs(r.h - 200.0f) <= 1.0f,
              "api: resize_to_frame() on a weighted slot gives it the frame asked for (got "
              + std::to_string(r.w) + "x" + std::to_string(r.h) + ")");
    }

    // Grid weights survive refresh(): journaled (latest value only), dropped by an
    // explicit set.
    void test_grid_ratio_journal() {
        std::printf("\n[grid ratios: the journal]\n");

        FigureEditBox box;
        box.update_figure([](FigureEdits& f) { f.col_ratios = std::vector<float>{1.0f, 2.0f}; });
        auto drained = box.load_and_clear_journaled();
        check(drained && drained->col_ratios && !drained->empty(),
              "journal: a weights-only edit survives the render thread's drain");
        box.update_figure([](FigureEdits& f) { f.col_ratios = std::vector<float>{1.5f, 1.5f}; });
        box.load_and_clear_journaled();
        auto j = box.take_journal();
        check(j && j->col_ratios && *j->col_ratios == std::vector<float>{1.5f, 1.5f}
              && !j->row_ratios && j->per_axes.empty(),
              "journal: it is journaled, and only the latest vector is kept");
        check(!box.take_journal().has_value(), "journal: taking it is destructive");

        box.update_figure([](FigureEdits& f) {
            f.col_ratios = std::vector<float>{2.0f, 1.0f};
            f.row_ratios = std::vector<float>{1.0f, 3.0f};
        });
        box.load_and_clear_journaled();
        box.update_figure([](FigureEdits& f) { f.col_ratios = std::vector<float>{9.0f, 1.0f}; });
        box.discard_ratios(true, false);
        auto after = box.load_and_clear();
        auto kept = box.take_journal();
        check(!after && kept && !kept->col_ratios && kept->row_ratios,
              "journal: an explicit column set drops the pending and journaled column weights only");
    }

    // The boundary under the cursor, and dragging it.
    void test_grid_boundary_drag() {
        std::printf("\n[grid ratios: boundary drag]\n");

        // 2x2 with the top row one span, so the column boundary exists only in
        // the bottom row.
        FigureSnapshot fs = grid_snapshot(2, 2);
        fs.axes.erase(fs.axes.begin() + 1);
        fs.axes[0].slot = AxesSlot{2, 2, 1, 2};
        const FigureLayout lay = compute_figure_layout(fs, GW, GH);
        const GridTracks t = grid_tracks(fs, lay.suptitle_band, GW, GH);

        const float col_bx = (t.col_x[0] + t.col_w[0] + t.col_x[1]) * 0.5f;
        const float row_by = (t.row_y[0] + t.row_h[0] + t.row_y[1]) * 0.5f;
        const float top_y = t.row_y[0] + t.row_h[0] * 0.5f;
        const float bot_y = t.row_y[1] + t.row_h[1] * 0.5f;
        const float mid_x0 = t.col_x[0] + t.col_w[0] * 0.5f;

        GridBoundary b = find_grid_boundary(fs, t, col_bx, bot_y, 4.0f);
        check(b.found && b.cols && b.k == 1, "boundary: the column boundary is found in the bottom row");
        check(!find_grid_boundary(fs, t, col_bx, top_y, 4.0f).found,
              "boundary: but not through the span above it");
        b = find_grid_boundary(fs, t, mid_x0, row_by, 4.0f);
        check(b.found && !b.cols && b.k == 1, "boundary: the row boundary is found under the span");
        check(!find_grid_boundary(fs, t, mid_x0, bot_y, 4.0f).found,
              "boundary: nothing in the middle of a cell");
        check(find_grid_boundary(fs, t, t.col_x[0] + t.col_w[0] - 3.0f, bot_y, 4.0f).found
              && !find_grid_boundary(fs, t, t.col_x[0] + t.col_w[0] - 6.0f, bot_y, 4.0f).found,
              "boundary: within the tolerance of the gap, and not beyond it");

        PanelState st;
        auto ptr = [](float x, float y) {
            PlotPointer p;
            p.x = x;
            p.y = y;
            p.hovered = true;
            return p;
        };

        // Hover: owns, shows the cursor, pushes nothing.
        GridDragOut o = update_grid_drag(st, fs, lay, GW, GH, ptr(col_bx, bot_y), 4.0f);
        check(o.owns && o.cursor_ew && !o.col_ratios && !st.grid_drag.active,
              "drag: hovering a boundary owns the pointer and shows a resize cursor");
        o = update_grid_drag(st, fs, lay, GW, GH, ptr(mid_x0, bot_y), 4.0f);
        check(!o.owns, "drag: a cell's interior is left to selection and navigation");

        // A pan already in progress crossing the boundary keeps the pointer.
        PlotPointer panning = ptr(col_bx, bot_y);
        panning.active = true;
        check(!update_grid_drag(st, fs, lay, GW, GH, panning, 4.0f).owns,
              "drag: a drag that began elsewhere is not taken over by crossing a boundary");

        // Press, then move 60 px right.
        PlotPointer press = ptr(col_bx, bot_y);
        press.pressed = press.active = true;
        o = update_grid_drag(st, fs, lay, GW, GH, press, 4.0f);
        check(o.owns && st.grid_drag.active && !o.col_ratios, "drag: a press on it starts a drag");
        PlotPointer hold = ptr(col_bx + 60.0f, bot_y);
        hold.active = true;
        o = update_grid_drag(st, fs, lay, GW, GH, hold, 4.0f);
        bool moved = false;
        if (o.col_ratios) {
            FigureSnapshot next = fs;
            next.col_ratios = *o.col_ratios;
            const GridTracks tn = grid_tracks(next, lay.suptitle_band, GW, GH);
            moved = near_px(tn.col_w[0], t.col_w[0] + 60.0f, 0.05f)
                    && near_px(tn.col_w[1], t.col_w[1] - 60.0f, 0.05f);
        }
        check(moved, "drag: moving 60 px moves the boundary 60 px, its neighbours' sum unchanged");
        check(!update_grid_drag(st, fs, lay, GW, GH, hold, 4.0f).col_ratios,
              "drag: a still cursor pushes nothing");

        // Far past the neighbour's edge: clamped to its minimum.
        PlotPointer far = ptr(col_bx + 5000.0f, bot_y);
        far.active = true;
        o = update_grid_drag(st, fs, lay, GW, GH, far, 4.0f);
        bool clamped = false;
        if (o.col_ratios) {
            FigureSnapshot next = fs;
            next.col_ratios = *o.col_ratios;
            const FigureLayout ln = compute_figure_layout(next, GW, GH);
            clamped = near_px(ln.cells.back().frame.w, kMinFrameSize, 0.05f);
        }
        check(clamped, "drag: dragged past its neighbour, the neighbour keeps the smallest frame");

        PlotPointer rel = ptr(col_bx, bot_y);
        rel.released = true;
        o = update_grid_drag(st, fs, lay, GW, GH, rel, 4.0f);
        check(o.owns && !st.grid_drag.active, "drag: the release ends it, and is still the drag's");

        // Double-click: the two tracks get equal weight.
        FigureSnapshot skew = fs;
        skew.row_ratios = {3.0f, 1.0f};
        const FigureLayout ls = compute_figure_layout(skew, GW, GH);
        const GridTracks ts = grid_tracks(skew, ls.suptitle_band, GW, GH);
        PlotPointer dbl = ptr(mid_x0, (ts.row_y[0] + ts.row_h[0] + ts.row_y[1]) * 0.5f);
        dbl.pressed = dbl.active = dbl.double_clicked = true;
        PanelState st2;
        o = update_grid_drag(st2, skew, ls, GW, GH, dbl, 4.0f);
        check(o.owns && o.row_ratios && *o.row_ratios == std::vector<float>{2.0f, 2.0f}
              && !st2.grid_drag.active,
              "drag: a double-click on a boundary gives its two tracks equal weight");
    }
} // namespace lt
