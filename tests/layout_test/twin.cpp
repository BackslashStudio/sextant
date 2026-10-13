// Twins (v1.2 step 36b-2): Axes::twinx()/twiny() -- a second axes over the
// same frame sharing one axis, its own on the opposite edge; the cell's one
// title, legend and colorbars; the shared color cycle; layout of both layers;
// and drawing in call order across them, raster and SVG. Part of
// sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "event_channel.h"
#include "plot_events.h"
#include "render_frame.h"
#include <regex>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 900, H = 600;

        const std::vector<double> kX1{0.0, 5.0, 10.0}, kY1{0.0, 1.0, 0.5};
        const std::vector<double> kX2{5.0, 12.0, 20.0}, kY2{100.0, 300.0, 200.0};

        bool same(Range a, Range b) { return a.lo == b.lo && a.hi == b.hi; }
        bool same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

        FigureSnapshot snap(Figure& f) { return detail::FigureAccess::impl(f).build_figure_snapshot(); }

        void test_api() {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            host->line(kX1, kY1);
            auto twin = host->twinx();
            twin->line(kX2, kY2);

            check(host->twinx() == twin, "twin: a second twinx() returns the same twin");
            bool threw = false;
            try { twin->twinx(); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "twin: twinx() on a twin throws std::invalid_argument");

            check(same(host->xlim(), twin->xlim()), "twin: a twinx shares its host's x range");
            check(host->xlim().hi >= 20.0, "twin: the automatic x range covers the twin's data");
            check(twin->ylim().lo >= 90.0 && host->ylim().hi <= 2.0, "twin: each keeps its own y range");
            twin->set_xlim(1.0, 4.0);
            check(same(host->xlim(), Range{1.0, 4.0}), "twin: set_xlim() on the twin moves the host");

            // The cell's title, legend and colorbar style are the host's.
            twin->set_title("Both");
            check(host->title() == "Both" && twin->title() == "Both", "twin: set_title() on a twin is the cell's");
            twin->legend();
            const FigureSnapshot fs = snap(*fig);
            check(fs.axes.size() == 1 && fs.axes[0].twins.size() == 1
                      && fs.axes[0].twins[0].layer == kLayerTwinX,
                  "twin: the snapshot holds the twin as a layer of its host's cell");
            check(fs.axes[0].snap2d()->legend_enabled && !fs.axes[0].twins[0].snap.legend_enabled,
                  "twin: legend() on a twin turns on the cell's legend");
            check(fs.axes[0].snap2d()->title == "Both" && fs.axes[0].twins[0].snap.title.empty(),
                  "twin: the title is stored on the host");
            check(!fs.axes[0].twins[0].snap.axes_style.show_xticks && fs.axes[0].twins[0].snap.axes_style.show_yticks,
                  "twin: a twinx's x axis starts hidden");
            check(fs.axes[0].twins[0].snap.xshare != 0 && fs.axes[0].twins[0].snap.xshare == fs.axes[0].snap2d()->xshare
                      && fs.axes[0].twins[0].snap.yshare == 0,
                  "twin: host and twinx are one x share group, y unshared");

            // One color cycle: the twin continues the host's colors.
            const auto cyc = default_color_cycle();
            check(same(*fs.axes[0].snap2d()->lines[0].opts.color, cyc[0])
                      && same(*fs.axes[0].twins[0].snap.lines[0].opts.color, cyc[1]),
                  "twin: the twin's first line takes the cycle's second color");

            // cla() on the host keeps the twin and restarts the shared cycle.
            host->cla();
            host->line(kX1, kY1);
            const FigureSnapshot after = snap(*fig);
            check(after.axes[0].twins.size() == 1 && after.axes[0].twins[0].snap.lines.size() == 1,
                  "twin: cla() on the host keeps the twin and its objects");
            check(same(*after.axes[0].snap2d()->lines[0].opts.color, cyc[0]),
                  "twin: cla() on the host restarts the cycle they share");
            twin->cla();
            check(!snap(*fig).axes[0].twins[0].snap.axes_style.show_xticks,
                  "twin: cla() on a twin keeps its x axis hidden");

            auto ty = host->twiny();
            const FigureSnapshot both = snap(*fig);
            check(both.axes[0].twins.size() == 2 && both.axes[0].twins[1].layer == kLayerTwinY
                      && both.axes[0].twins[1].snap.yshare == both.axes[0].snap2d()->yshare
                      && !both.axes[0].twins[1].snap.axes_style.show_yticks,
                  "twin: a twiny is the second layer, shares y and hides its y axis");

            // Edits address a twin by axes_key().
            auto& fi = detail::FigureAccess::impl(*fig);
            check(fi.find_slot_impl(axes_key(1, kLayerTwinX)) != nullptr
                      && fi.find_slot_impl(axes_key(1, kLayerTwinY)) != nullptr
                      && fi.find_slot_impl(axes_key(2, kLayerTwinX)) == nullptr,
                  "twin: find_slot_impl() decodes a twin's key");
            check(both.find2d(axes_key(1, kLayerTwinY)) == &both.axes[0].twins[1].snap,
                  "twin: FigureSnapshot::find2d() decodes it too");

            bool big = false;
            try { Figure::create()->add_subplot(1024, 1024, 1); } catch (const std::invalid_argument&) { big = true; }
            check(big, "twin: add_subplot() refuses a grid of 2^20 cells (edit keys hold the layer above)");

            // A twin the caller keeps outlives its host as a plain axes.
            std::shared_ptr<Axes> kept;
            {
                auto f2 = Figure::create();
                kept = f2->add_subplot(1, 1, 1)->twinx();
            }
            kept->set_title("alone");
            check(kept->title() == "alone", "twin: a twin outliving its host keeps working on its own");
        }

        void test_layout() {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            host->line(kX1, kY1).set_ytitle("host y");
            AxesStyle hs;
            hs.yaxis_x = AxisPosition::High;   // ignored while twinned
            host->set_axes_style(hs);
            const FigureLayout alone = compute_figure_layout(snap(*fig), W, H);
            auto twin = host->twinx();
            twin->line(kX2, kY2).set_ytitle("twin y");
            const FigureLayout l = compute_figure_layout(snap(*fig), W, H);
            const CellLayout& c = l.cells[0];

            check(c.twins.size() == 1 && c.twins[0].layer == kLayerTwinX, "twin layout: one twin layout");
            const CellLayout& t = c.twins[0];
            check(t.frame.x == c.frame.x && t.frame.y == c.frame.y && t.frame.w == c.frame.w && t.frame.h == c.frame.h,
                  "twin layout: one frame for both layers");
            check(near_px(c.yaxis_x, c.frame.x) && near_px(t.yaxis_x, c.frame.x + c.frame.w),
                  "twin layout: host y on the left (its yaxis_x ignored), twin y on the right");
            check(t.ylabel_align == HAlign::Left && t.ylabel_x > c.frame.x + c.frame.w,
                  "twin layout: the twin's labels face out on the right");
            check(c.ytitle_x < c.frame.x && t.ytitle_x > t.ylabel_x,
                  "twin layout: each y title on its own axis' side");
            check(alone.cells[0].yaxis_x > alone.cells[0].frame.x + alone.cells[0].frame.w - 1.0f,
                  "twin layout: (untwinned, the host's yaxis_x = High holds)");
            std::printf("  host y %g..%g, twin y %g..%g\n", c.tr.ymin, c.tr.ymax, t.tr.ymin, t.tr.ymax);
            check(t.tr.xmin == c.tr.xmin && t.tr.xmax == c.tr.xmax && t.tr.ymin > 80.0 && c.tr.ymax < 2.0,
                  "twin layout: one x transform, two y ones");
            check(!t.xgrid && t.ygrid && c.xgrid && c.ygrid, "twin layout: the twin's grid draws y only");
            check(c.frame.x + c.frame.w < alone.cells[0].frame.x + alone.cells[0].frame.w - 20.0f,
                  "twin layout: the twin's band and title take room on the right");

            // The inverse holds for a twinned cell.
            const FigureSnapshot fs = snap(*fig);
            const LayoutSize sz = figure_size_for_frame(fs, 1, 400.0f, 250.0f);
            const FigureLayout inv = compute_figure_layout(fs, int(std::lround(sz.width)), int(std::lround(sz.height)));
            check(near_px(inv.cells[0].frame.w, 400.0f, 1.0f) && near_px(inv.cells[0].frame.h, 250.0f, 1.0f),
                  "twin layout: figure_size_for_frame() gives a twinned cell its frame");

            // twiny: the twin's x at the top, with its title above.
            auto f2 = Figure::create();
            auto h2 = f2->add_subplot(1, 1, 1);
            h2->line(kX1, kY1);
            auto ty = h2->twiny();
            ty->line(kY2, kX1).set_xtitle("twin x");
            const FigureLayout l2 = compute_figure_layout(snap(*f2), W, H);
            const CellLayout& c2 = l2.cells[0];
            std::printf("  twiny: frame y %g..%g, host x axis %g, twin x axis %g\n", c2.frame.y,
                        c2.frame.y + c2.frame.h, c2.xaxis_y, c2.twins.empty() ? -1.0f : c2.twins[0].xaxis_y);
            check(c2.twins.size() == 1 && near_px(c2.twins[0].xaxis_y, c2.frame.y) && near_px(c2.xaxis_y, c2.frame.y + c2.frame.h),
                  "twin layout: twiny x on top, host x at the bottom");
            check(c2.twins[0].xtitle_y < c2.frame.y && c2.twins[0].xlabel_top < c2.frame.y,
                  "twin layout: the twiny's labels and title above the frame");

            // One legend over both layers in call order; colorbars host's then twin's.
            auto f3 = Figure::create();
            auto h3 = f3->add_subplot(1, 1, 1);
            auto t3 = h3->twinx();
            h3->line(kX1, kY1, {.name = "a"});
            t3->line(kX2, kY2, {.name = "b"});
            h3->line(kX1, kY1, {.name = "c"});
            const std::vector<double> z{0.0, 0.5, 1.0};
            ScatterZOptions zo;
            zo.colorbar = true;
            zo.name = "tz";
            t3->scatter_z(kX2, kY2, z, zo);
            zo.name = "hz";
            h3->scatter_z(kX1, kY1, z, zo);
            t3->legend();
            const FigureSnapshot s3 = snap(*f3);
            const auto e = collect_legend_entries(s3.axes[0]);
            check(e.size() == 5 && e[0].name == "a" && e[1].name == "b" && e[2].name == "c" && e[3].name == "tz"
                      && e[4].name == "hz",
                  "twin layout: the legend lists both layers' series in call order");
            const auto cb = cell_colorbar_requests(s3.axes[0]);
            check(cb.size() == 2 && cb[0].name == "hz" && cb[1].name == "tz",
                  "twin layout: colorbars are the host's requests, then the twin's");
            const FigureLayout l3 = compute_figure_layout(s3, W, H);
            const CellLayout& c3 = l3.cells[0];
            check(c3.legend_entries.size() == 5 && c3.colorbars.size() == 2,
                  "twin layout: the cell lays out that legend and both bars");
        }

        std::size_t count_rgb(const RgbaImage& im, int r, int g, int b) {
            std::size_t n = 0;
            for (std::size_t i = 0; i + 3 < im.pixels.size(); i += 4)
                if (std::abs(int(im.pixels[i]) - r) <= 6 && std::abs(int(im.pixels[i + 1]) - g) <= 6
                    && std::abs(int(im.pixels[i + 2]) - b) <= 6)
                    ++n;
            return n;
        }

        void test_render() {
            const std::vector<double> x{5.0}, y{5.0}, z{0.0};
            ScatterOptions red;
            red.color = Color{1.0f, 0.0f, 0.0f, 1.0f};
            red.size = 40.0f;
            red.marker = MarkerStyle::Square;
            red.alpha = 1.0f;
            ScatterZOptions purple;
            purple.size = 40.0f;
            purple.marker = MarkerStyle::Square;
            purple.alpha = 1.0f;
            // The host's square, the twin's over the same pixels: the later call covers.
            auto squares = [&](bool twin_last, bool svg) {
                auto fig = Figure::create({.width = 300, .height = 300});
                auto host = fig->add_subplot(1, 1, 1);
                host->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
                auto twin = host->twinx();
                twin->set_ylim(0.0, 10.0);
                if (twin_last) {
                    host->scatter(x, y, red);
                    twin->scatter_z(x, y, z, purple);
                } else {
                    twin->scatter_z(x, y, z, purple);
                    host->scatter(x, y, red);
                }
                return std::make_pair(fig->render_rgba(), svg ? fig->render_svg().svg : std::string());
            };
            const auto a = squares(true, true), b = squares(false, true);
            const std::size_t ra = count_rgb(a.first, 255, 0, 0), pa = count_rgb(a.first, 68, 1, 84);
            const std::size_t rb = count_rgb(b.first, 255, 0, 0), pb = count_rgb(b.first, 68, 1, 84);
            std::printf("  host then twin: %zu red, %zu purple; twin then host: %zu red, %zu purple\n", ra, pa, rb, pb);
            check(ra < 20 && pa > 800, "twin render: the twin's object made later covers the host's");
            check(pb < 20 && rb > 800, "twin render: and the host's made later covers the twin's");
            // SVG writes them in the same order (the red square as a <rect> with fill rgb(255,0,0)).
            const auto pos = [](const std::string& s, const char* what) { return s.find(what); };
            const auto red_a = pos(a.second, "rgb(255,0,0)"), pur_a = pos(a.second, "rgb(68,1,84)");
            const auto red_b = pos(b.second, "rgb(255,0,0)"), pur_b = pos(b.second, "rgb(68,1,84)");
            check(red_a != std::string::npos && pur_a != std::string::npos && red_a < pur_a
                      && red_b != std::string::npos && pur_b != std::string::npos && pur_b < red_b,
                  "twin render: SVG writes both layers in call order too");

            // The twin draws no frame or background; its grid only its y lines.
            auto fig = Figure::create({.width = 300, .height = 300});
            auto host = fig->add_subplot(1, 1, 1);
            host->line(kX1, kY1);
            auto twin = host->twinx();
            twin->line(kX2, kY2);
            // The twin's own background field, which only a panel edit reaches
            // (set_axes_style() on a twin gives the background to its host).
            {
                auto& fi = detail::FigureAccess::impl(*fig);
                AxesStyle ts = snap(*fig).axes[0].twins[0].snap.axes_style;
                ts.background = Color{0.0f, 1.0f, 0.0f, 1.0f};
                FigureEdits e;
                AxesEdit ae;
                ae.axes_style = ts;
                e.per_axes.push_back({axes_key(1, kLayerTwinX), ae});
                fi.apply_edits(e);
                fi.apply_edits_and_publish();
                const FigureSnapshot s = snap(*fig);
                check(s.axes[0].twins[0].snap.axes_style.background.r == 0.0f
                          && s.axes[0].snap2d()->axes_style.background.r == 1.0f,
                      "twin render: (a panel edit sets the twin's own background, not the host's)");
            }
            GridOptions go;
            go.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            twin->grid(true, go);
            const RgbaImage im = fig->render_rgba();
            check(count_rgb(im, 0, 255, 0) == 0, "twin render: the twin's background is never drawn");
            const std::string svg = fig->render_svg().svg;
            const std::size_t g0 = svg.find("<g stroke=\"rgb(0,0,255)\"");
            const std::size_t g1 = g0 == std::string::npos ? g0 : svg.find("</g>", g0);
            int vertical = 0, horizontal = 0;
            if (g1 != std::string::npos) {
                const std::string grp = svg.substr(g0, g1 - g0);
                const std::regex line(R"re(<line x1="([^"]+)" y1="([^"]+)" x2="([^"]+)" y2="([^"]+)")re");
                for (auto it = std::sregex_iterator(grp.begin(), grp.end(), line); it != std::sregex_iterator(); ++it) {
                    if ((*it)[1] == (*it)[3]) ++vertical;
                    if ((*it)[2] == (*it)[4]) ++horizontal;
                }
            }
            check(vertical == 0 && horizontal > 0, "twin render: the twin's grid draws its y lines only (SVG)");
        }
        // Edits address a twin by axes_key(): the snapshot patch, the caller's
        // replay, the group sync and undo.
        void test_edits() {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            host->line(kX1, kY1);
            auto twin = host->twinx();
            twin->line(kX2, kY2);
            auto& fi = detail::FigureAccess::impl(*fig);
            const int tk = axes_key(1, kLayerTwinX);

            FigureEdits e;
            AxesEdit ae;
            ae.ymin = 50.0; ae.ymax = 60.0; ae.ylim_auto = false;
            ae.xmin = 2.0; ae.xmax = 3.0; ae.xlim_auto = false;
            e.per_axes.push_back({tk, ae});
            const FigureEdits undo = fi.apply_edits(e);
            const auto s = fi.snapshot_box.load();
            const RenderSnapshot& h = *s->axes[0].snap2d();
            const RenderSnapshot& t = s->axes[0].twins[0].snap;
            check(t.ymin == 50.0 && t.ymax == 60.0 && !t.ylim_auto && h.ylim_auto,
                  "twin edits: a keyed edit reaches the twin's snapshot, not the host's y");
            check(h.xmin == 2.0 && h.xmax == 3.0 && !h.xlim_auto,
                  "twin edits: its x limits reach the host through the group");
            fi.apply_edits_and_publish();
            check(same(twin->ylim(), Range{50.0, 60.0}) && same(host->xlim(), Range{2.0, 3.0}),
                  "twin edits: the caller's replay lands on the live twin and host");
            check(undo.per_axes.size() == 1 && undo.per_axes[0].first == tk,
                  "twin edits: the inverse is addressed to the twin");
            fi.apply_edits(undo);
            const auto u = fi.snapshot_box.load();
            check(u->axes[0].twins[0].snap.ylim_auto && u->axes[0].snap2d()->xlim_auto,
                  "twin edits: undo restores the twin and, through the group, the host");
        }

        ImGuiContext* bare_imgui(float w, float h) {
            ImGuiContext* ictx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ictx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(w, h);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();
            io.MousePos = ImVec2(-1000.0f, -1000.0f);
            return ictx;
        }

        // A drag and a wheel over a twinned cell move both layers, each through
        // its own transform.
        void test_navigation() {
            GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});
            RenderDevice dev;
            PlotView view;

            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            host->set_xlim(0.0, 10.0).set_ylim(0.0, 1.0);
            auto twin = host->twinx();
            twin->set_ylim(100.0, 300.0);
            const FigureSnapshot fs = snap(*fig);
            FigureEditBox box;
            FigureWindowState st;
            st.plot.navigate_enabled = true;
            ImGuiContext* ictx = bare_imgui(900.0f, 700.0f);
            FigureContext ctx = panel_ctx(fs, box, st);
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
                ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
                push_figure_id(ctx.figure_id);
                st.plot_info = view.draw(ctx, dev, st.plot,
                                         PlotViewParams{.size = ImVec2(420.0f, 300.0f), .display_scale = 1.0f,
                                                        .supersample = 1});
                ImGui::PopID();
                ImGui::End();
                ImGui::Render();
            };
            ImGuiIO& io = ImGui::GetIO();
            frame();
            // The frame's centre, in window coordinates (the image sits at the
            // window's cursor start).
            const CellLayout c = compute_figure_layout(fs, *st.plot_info.measure, 420, 300).cells[0];
            const ImVec2 origin = ImGui::GetStyle().WindowPadding;
            const float cx = origin.x + c.frame.x + c.frame.w * 0.5f, cy = origin.y + c.frame.y + c.frame.h * 0.5f;
            io.AddMousePosEvent(cx, cy);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(cx, cy + 30.0f);
            frame();
            io.AddMousePosEvent(cx, cy + 60.0f);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            const auto pan = box.load_and_clear();
            const auto find = [](const std::optional<FigureEdits>& f, int key) -> const AxesEdit* {
                if (!f) return nullptr;
                for (const auto& [k, e] : f->per_axes)
                    if (k == key) return &e;
                return nullptr;
            };
            const AxesEdit* he = find(pan, 1);
            const AxesEdit* te = find(pan, axes_key(1, kLayerTwinX));
            std::printf("  pan: host y %g..%g, twin y %g..%g\n", he && he->ymin ? *he->ymin : -1.0,
                        he && he->ymax ? *he->ymax : -1.0, te && te->ymin ? *te->ymin : -1.0,
                        te && te->ymax ? *te->ymax : -1.0);
            const bool both = he && te && he->ymin && te->ymin && he->ymax && te->ymax;
            check(both, "twin navigation: a drag pushes limits for the host and the twin");
            if (both) {
                // Dragged down: both views move up by the same share of their span.
                const double hs = (*he->ymin - 0.0) / 1.0, ts = (*te->ymin - 100.0) / 200.0;
                check(hs > 0.05 && std::abs(hs - ts) < 1e-6,
                      "twin navigation: each layer moves by the same pixels through its own scale");
                check(te->xmin && he->xmin && *te->xmin == *he->xmin,
                      "twin navigation: the shared x moves alike in both");
            }

            io.AddMouseWheelEvent(0.0f, 1.0f);
            frame();
            const auto zoom = box.load_and_clear();
            const AxesEdit* hz = find(zoom, 1);
            const AxesEdit* tz = find(zoom, axes_key(1, kLayerTwinX));
            check(hz && tz && hz->ymax && tz->ymax && tz->ymin && (*tz->ymax - *tz->ymin) < 199.0,
                  "twin navigation: the wheel zooms the twin too");

            // The resolved limits a panel tracks include the twin's, by key.
            check(st.plot_info.resolved_for(axes_key(1, kLayerTwinX)) != nullptr,
                  "twin navigation: the view reports the twin's resolved limits by key");
            ImGui::DestroyContext(ictx);
            ImGui::SetCurrentContext(nullptr);
        }

        void test_pick() {
            auto fig = Figure::create({.width = 400, .height = 300});
            auto host = fig->add_subplot(1, 1, 1);
            host->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
            auto twin = host->twinx();
            twin->set_ylim(0.0, 100.0);
            const std::vector<double> hx{2.0}, hy{2.0}, tx{8.0}, ty{80.0};
            host->scatter(hx, hy);
            twin->scatter(tx, ty);
            const FigureSnapshot fs = snap(*fig);
            const std::vector<AxesLayout> layout = axes_layouts(compute_figure_layout(fs, 400, 300));
            const AxesLayout& cell = layout[0];
            check(cell.twins.size() == 1 && cell.twins[0].layer == kLayerTwinX,
                  "twin pick: the drawn layout lists the twin's transform");
            const CellLayers l = cell_layers(fs.axes[0], cell);
            std::size_t k = 9;
            const float px = cell.twins[0].tr.to_px(8.0), py = cell.twins[0].tr.to_py(80.0);
            const auto hit = find_pick_layers(l, px + 2.0f, py, k);
            check(hit && k == 1 && hit->kind == PickKind::Scatter && hit->object == 0,
                  "twin pick: a twin's point is found through the twin's transform");
            check(hit && format_pick(*l.snaps[k], *hit).find("80") != std::string::npos,
                  "twin pick: and its hint shows the twin's own y");
            const auto near_host = find_pick_layers(l, cell.tr.to_px(2.0), cell.tr.to_py(2.0), k);
            check(near_host && k == 0, "twin pick: a host point the host's");

            // Same pixel: the later call wins, whichever axes took it.
            auto f2 = Figure::create({.width = 400, .height = 300});
            auto h2 = f2->add_subplot(1, 1, 1);
            h2->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
            auto t2 = h2->twinx();
            t2->set_ylim(0.0, 10.0);
            const std::vector<double> p{5.0};
            t2->scatter(p, p);
            h2->scatter(p, p);
            const FigureSnapshot fs2 = snap(*f2);
            const std::vector<AxesLayout> l2 = axes_layouts(compute_figure_layout(fs2, 400, 300));
            const auto tie = find_pick_layers(cell_layers(fs2.axes[0], l2[0]), l2[0].tr.to_px(5.0),
                                              l2[0].tr.to_py(5.0), k);
            check(tie && k == 0, "twin pick: of two at one pixel the later call wins (the host's here)");

            // Events: y2data, and pick_twin.
            const PointLocation loc = locate_point(fs, layout, px, py);
            check(loc.has_data && std::abs(loc.y2 - 80.0) < 0.5 && std::isnan(loc.x2),
                  "twin events: a pointer reports the twinx's y as y2data, x2data NaN");
            PlotEventTracker tr;
            PlotInputFrame in;
            in.x = px; in.y = py; in.hovered = true;
            in.down[0] = true;
            std::vector<Event> out;
            collect_plot_events(tr, in, PlotEventInfo{}, fs, layout, event_bit(EventKind::Pick), out);
            check(out.size() == 1 && out[0].kind == EventKind::Pick && out[0].pick_twin == 1
                      && out[0].pick_object == 0,
                  "twin events: a pick on the twin's point says pick_twin = 1");
        }

        // The inspectors edit one layer at a time.
        void test_panel() {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            host->line(kX1, kY1);
            auto twin = host->twinx();
            twin->set_ylim(100.0, 300.0);
            twin->scatter(kX2, kY2);
            const FigureSnapshot fs = snap(*fig);
            FigureEditBox box;
            FigureWindowState st;
            ImGuiContext* ictx = bare_imgui(900.0f, 700.0f);
            st.selection.select_layer(kLayerTwinX);
            ImGui::NewFrame();
            draw_cosmetic_window(fs, box, st);
            ImGui::Render();
            check(st.slot_view.ymin_local == 100.0 && st.slot_view.ymax_local == 300.0,
                  "twin panel: the Limits fields seed from the selected twin");
            DataPanelState d;
            pull_data_panel(d, st.selection, fs.axes[0]);
            check(d.sheet_local.scatters.size() == 1 && d.sheet_local.lines.empty(),
                  "twin panel: the Data panel lists the selected twin's objects");
            check(st.selection.key() == axes_key(1, kLayerTwinX), "twin panel: and edits address it");

            // A layer the cell lacks falls back to the host.
            st.selection.select_layer(kLayerTwinY);
            normalize_selection(st.selection, fs);
            check(st.selection.layer == 0, "twin panel: a missing twin selects the host");
            st.selection.select_layer(kLayerTwinX);
            st.selection.select(2);
            check(st.selection.layer == 0, "twin panel: another slot starts on its host");
            ImGui::DestroyContext(ictx);
            ImGui::SetCurrentContext(nullptr);
        }
    } // namespace

    void test_twin() {
        std::printf("\n[twins: twinx()/twiny(), the cell's layers]\n");
        test_api();
        test_layout();
        test_render();
        test_edits();
        test_navigation();
        test_pick();
        test_panel();
    }
} // namespace lt
