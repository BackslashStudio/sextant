// The Save and Resize dialogs (GUI-kit R4): how their fields resolve into
// requests, perform_save() against the exporters it wraps, and the dialogs drawn
// in a host's window. The first automated coverage of this path.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "figure_impl.h"

#include <mutex>

namespace lt {
    using namespace sextant;

    namespace {
        // Two 2D slots whose frames differ for one figure size: slot 1 spans the
        // top row of a 2 x 2 grid, slot 3 is the bottom-left cell. (Frames in
        // one row or column are aligned, so a title alone would not differ.)
        FigureSnapshot two_slot_snapshot() {
            FigureSnapshot fs = make_snapshot(2, 2, 2);
            fs.axes[0].slot = AxesSlot{2, 2, 1, 2};
            fs.axes[1].slot = AxesSlot{2, 2, 3};
            fs.generation = fs.data_generation = 1;
            return fs;
        }

        void set_live(PlotViewState& pv, int w, int h) {
            pv.live_plot_w.store(w);
            pv.live_plot_h.store(h);
        }

        int rounded(float v) { return static_cast<int>(std::lround(v)); }

        std::string read_all(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        }

        bool exists(const std::string& path) {
            std::error_code ec;
            return std::filesystem::exists(path, ec);
        }

        void remove_file(const std::string& path) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }

        // Messages reaching the handler while it is installed.
        struct Captured {
            std::mutex m;
            std::vector<std::string> msgs;
            Captured() {
                Figure::set_message_handler([this](std::string_view s) {
                    std::lock_guard<std::mutex> lock(m);
                    msgs.emplace_back(s);
                });
            }
            ~Captured() { Figure::set_message_handler(nullptr); }
            std::vector<std::string> take() {
                std::lock_guard<std::mutex> lock(m);
                return std::exchange(msgs, {});
            }
        };

        // A plane cutting a bar grid: its SVG order needs splits, so a
        // max_splits of 1 binds (test_export_budget()'s scene).
        FigureSnapshot inexact_3d_snapshot() {
            auto fig = Figure::create({.width = 420, .height = 360, .title = "dialogs"});
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->set_view(-55.0, 24.0);
            std::vector<double> u(5), v(5), z(25);
            for (int i = 0; i < 5; ++i) u[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / 4;
            for (int j = 0; j < 5; ++j) v[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / 4;
            for (std::size_t k = 0; k < z.size(); ++k)
                z[k] = 0.6 + 0.4 * std::sin(static_cast<double>(k));
            ax->bar3d(PlaneOrientation::XY, u, v, z,
                      {.color = Color::Orange, .width = 0.7f, .depth = 0.7f, .bottom = -1.0});
            std::vector<double> m(16 * 16);
            for (std::size_t k = 0; k < m.size(); ++k)
                m[k] = std::sin(0.4 * static_cast<double>(k));
            ax->plane(PlaneOrientation::YZ, 0.0, {.alpha = 0.6f})
                    ->heatmap(m, 16, 16, {-3.0, 3.0}, {-1.0, 1.2});
            return detail::FigureAccess::impl(*fig).build_figure_snapshot();
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Resolution and prefill: no ImGui, no GL
    // -------------------------------------------------------------------------
    void test_dialog_requests() {
        std::printf("\n[dialogs: requests resolved apart from the UI]\n");

        const FigureSnapshot fs = two_slot_snapshot();
        Selection sel;
        SlotViewState sv;
        FigureEditBox box;
        PlotViewState pv;
        set_live(pv, 640, 480);
        FigureContext ctx{ fs, box, sel, sv, &pv, 1 };
        const FigureMeasure fresh = measure_figure(fs);

        // ---- Save, figure mode.
        {
            SaveDialogState st;
            std::snprintf(st.path_buf, sizeof(st.path_buf), "%s", "out/plot.svg");
            st.max_splits = 7;
            st.peel_layers = 3;
            const auto r = resolve_save_request(st, ctx);
            check(r && r->width == 640 && r->height == 480,
                  "save request: a size <= 0 is the plot view's live size");
            check(r && r->path == "out/plot.svg" && r->max_splits == 7 && r->peel_layers == 3,
                  "save request: and the path and export bounds are carried as entered");

            st.width = 800;
            const auto r1 = resolve_save_request(st, ctx);
            check(r1 && r1->width == 800 && r1->height == 480,
                  "save request: each side falls back on its own");

            st.width = 900;
            st.height = 700;
            const auto r2 = resolve_save_request(st, ctx);
            check(r2 && r2->width == 900 && r2->height == 700,
                  "save request: an entered figure size is taken as it is");
        }

        // ---- Save, frame mode: through figure_size_for_frame() for the
        // selected slot.
        {
            SaveDialogState st;
            st.size_mode = SaveDialogState::SizeMode::PlotFrame;
            st.width = 300;
            st.height = 200;
            const LayoutSize s1 = figure_size_for_frame(fs, fresh, 1, 300.0f, 200.0f);
            const LayoutSize s2 = figure_size_for_frame(fs, fresh, 3, 300.0f, 200.0f);
            const auto r1 = resolve_save_request(st, ctx);
            check(r1 && r1->width == rounded(s1.width) && r1->height == rounded(s1.height),
                  "save request: frame mode is the figure size whose selected frame is that size");
            sel.select(3);
            const auto r2 = resolve_save_request(st, ctx);
            check(r2 && r2->width == rounded(s2.width) && r2->height == rounded(s2.height),
                  "save request: for the slot selected now");
            check(rounded(s1.width) != rounded(s2.width),
                  "save request: (control) the two slots' frames do ask for different figures");
            sel.select(1);

            // An empty frame size is the live figure size, as in figure mode.
            st.width = 0;
            st.height = 0;
            const auto r3 = resolve_save_request(st, ctx);
            check(r3 && r3->width == 640 && r3->height == 480,
                  "save request: frame mode with no size saves the live figure size");
        }

        // ---- Save, frame mode, with the view's stored measure: one fitted on a
        // titled copy, resolved against the untitled snapshot, so the stored
        // and a fresh measure differ.
        {
            FigureSnapshot titled = fs;
            titled.axes[0].snap2d()->title = "Titled";
            PlotViewState stored;
            set_live(stored, 640, 480);
            (void)stored.layout.fit(titled, 640, 480);
            FigureContext sctx{ fs, box, sel, sv, &stored, 1 };
            SaveDialogState st;
            st.size_mode = SaveDialogState::SizeMode::PlotFrame;
            st.width = 300;
            st.height = 200;
            const auto m = stored.layout.load();
            const LayoutSize want = m ? figure_size_for_frame(fs, *m, 1, 300.0f, 200.0f)
                                      : LayoutSize{};
            const LayoutSize afresh = figure_size_for_frame(fs, fresh, 1, 300.0f, 200.0f);
            const auto r = resolve_save_request(st, sctx);
            check(m && r && r->width == rounded(want.width) && r->height == rounded(want.height),
                  "save request: frame mode converts with the view's stored measure");
            check(rounded(want.height) != rounded(afresh.height),
                  "save request: (control) which is not what a fresh measure gives");
        }

        // ---- Unknown sizes give no request, not a 0 x 0 one.
        {
            SaveDialogState st;
            FigureContext none{ fs, box, sel, sv, nullptr, 1 };
            check(!resolve_save_request(st, none),
                  "save request: with no plot view on screen, an empty size is no request");
            st.width = 500;
            check(!resolve_save_request(st, none),
                  "save request: nor is a half-entered one");
            st.height = 400;
            const auto r = resolve_save_request(st, none);
            check(r && r->width == 500 && r->height == 400,
                  "save request: but an entered figure size needs no view");

            PlotViewState unseen;   // before its first frame
            FigureContext early{ fs, box, sel, sv, &unseen, 1 };
            check(!resolve_save_request(SaveDialogState{}, early),
                  "save request: before the view's first frame, an empty size is no request");
        }

        // ---- Resize: a frame size to a plot size, for the selected slot.
        {
            ResizeDialogState st;
            check(!resolve_resize_request(st, ctx), "resize request: no frame size, no request");
            st.frame_w = 300;
            st.frame_h = 200;
            sel.select(3);
            const LayoutSize s = figure_size_for_frame(fs, fresh, 3, 300.0f, 200.0f);
            const auto r = resolve_resize_request(st, ctx);
            check(r && r->plot_w == rounded(s.width) && r->plot_h == rounded(s.height),
                  "resize request: the plot size whose selected frame is the size entered");
            sel.select(1);
        }

        // ---- The prefill.
        {
            SaveDialogState st;
            open_save_dialog(st, ctx);
            check(st.open && st.width == 640 && st.height == 480,
                  "save prefill: opening the dialog fills in the live plot size");
            st.width = 123;
            open_save_dialog(st, ctx);
            check(st.width == 123, "save prefill: re-opening an open dialog keeps what was typed");

            SaveDialogState blind;
            FigureContext none{ fs, box, sel, sv, nullptr, 1 };
            open_save_dialog(blind, none);
            check(blind.open && blind.width == 0 && blind.height == 0,
                  "save prefill: with no plot view, 0 (the live size, once there is one)");
        }
        {
            const FigureLayout fl = compute_figure_layout(fs, fresh, 640, 480);
            for (int slot : {1, 3}) {
                sel.select(slot);
                ResizeDialogState st;
                open_resize_dialog(st, ctx);
                const PlotRect& f = fl.cells[slot == 1 ? 0 : 1].frame;
                check(st.open && st.frame_w == rounded(f.w) && st.frame_h == rounded(f.h),
                      "resize prefill: the selected axes' current frame (slot " +
                      std::to_string(slot) + ")");
                // Apply on the prefill asks for the size the window has now.
                const auto r = resolve_resize_request(st, ctx);
                check(r && std::abs(r->plot_w - 640) <= 1 && std::abs(r->plot_h - 480) <= 1,
                      "resize prefill: applied unchanged, it asks for the current plot size "
                      "(slot " + std::to_string(slot) + ")");
            }
            sel.select(1);

            ResizeDialogState blind;
            FigureContext none{ fs, box, sel, sv, nullptr, 1 };
            open_resize_dialog(blind, none);
            check(blind.open && blind.frame_w == 0 && blind.frame_h == 0,
                  "resize prefill: with no plot view, nothing to prefill");
        }
    }

    // -------------------------------------------------------------------------
    // perform_save(): the exporters, never an exception
    // -------------------------------------------------------------------------
    void test_perform_save() {
        std::printf("\n[dialogs: perform_save()]\n");

        GLContext gl({.width = 200, .height = 150, .title = "layout_test", .visible = false});
        NvgRenderer nvg(gl.nvg());
        DataRenderer data;

        const FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        const FigureMeasure on_screen = measure_figure(fs);
        Captured inbox;

        // ---- SVG and PNG, the same bytes as the exporters'.
        {
            const SaveRequest req{ .path = "r4_save.svg", .width = 420, .height = 300 };
            const SaveResult r = perform_save(gl, nvg, data, fs, req, &on_screen, 1, 1.0f);
            export_figure_svg(fs, "r4_direct.svg", 420, 300, {}, nullptr, &on_screen);
            check(r.written && r.exact && r.warning.empty(),
                  "perform_save: an SVG is written, exact, with no warning");
            const std::string a = read_all("r4_save.svg");
            check(!a.empty() && a == read_all("r4_direct.svg"),
                  "perform_save: byte-identical to export_figure_svg()");
        }
        {
            const SaveRequest req{ .path = "r4_save.png", .width = 420, .height = 300,
                                   .peel_layers = 4 };
            const SaveResult r = perform_save(gl, nvg, data, fs, req, &on_screen, 2, 1.5f);
            export_figure_png(gl, nvg, data, fs, "r4_direct.png", 420, 300, 2, 4, &on_screen, 1.5f);
            check(r.written && r.exact && r.warning.empty(),
                  "perform_save: a PNG is written, exact, with no warning");
            check(exists("r4_save.png") && same_picture("r4_save.png", "r4_direct.png"),
                  "perform_save: the same picture as export_figure_png(), supersample and "
                  "scale passed through");
            if (renderer_repeats_exactly())
                check(read_all("r4_save.png") == read_all("r4_direct.png"),
                      "perform_save: byte-identical where the renderer repeats itself");
            int w = 0, h = 0, n = 0;
            if (unsigned char* px = stbi_load("r4_save.png", &w, &h, &n, 4)) stbi_image_free(px);
            check(w == 630 && h == 450, "perform_save: at png_scale times the figure size");
        }
        check(inbox.take().empty(), "perform_save: a good save sends no message");

        // ---- Failures: written = false, the exporter's sentence, the handler told.
        {
            const SaveRequest req{ .path = "r4_save.txt", .width = 420, .height = 300 };
            const SaveResult r = perform_save(gl, nvg, data, fs, req, &on_screen, 1, 1.0f);
            check(!r.written && r.warning.find("must end in .png or .svg") != std::string::npos,
                  "perform_save: a bad extension is not written, and says why");
            check(!exists("r4_save.txt"), "perform_save: and no file appears");
            const auto msgs = inbox.take();
            check(msgs.size() == 1 && msgs[0] == "Save failed: " + r.warning,
                  "perform_save: the message handler gets \"Save failed: \" and the sentence");
        }
        {
            const std::string bad = "r4_no_such_dir/plot.svg";
            std::string direct;
            try {
                export_figure_svg(fs, bad, 420, 300, {}, nullptr, &on_screen);
            } catch (const std::exception& ex) {
                direct = ex.what();
            }
            const SaveRequest req{ .path = bad, .width = 420, .height = 300 };
            SaveResult r;
            bool threw = false;
            try {
                r = perform_save(gl, nvg, data, fs, req, &on_screen, 1, 1.0f);
            } catch (...) {
                threw = true;
            }
            check(!threw, "perform_save: an unwritable path does not throw");
            check(!direct.empty() && !r.written && r.warning == direct,
                  "perform_save: it comes back unwritten, with the exporter's own sentence");
            const auto msgs = inbox.take();
            check(msgs.size() == 1 && msgs[0] == "Save failed: " + direct,
                  "perform_save: and the handler is told");

            const SaveRequest png{ .path = "r4_no_such_dir/plot.png", .width = 420, .height = 300 };
            const SaveResult rp = perform_save(gl, nvg, data, fs, png, &on_screen, 1, 1.0f);
            check(!rp.written && !rp.warning.empty() && inbox.take().size() == 1,
                  "perform_save: the same for a PNG");
        }
        {
            const SaveRequest req{ .path = "r4_empty.svg", .width = 0, .height = 300 };
            const SaveResult r = perform_save(gl, nvg, data, fs, req, &on_screen, 1, 1.0f);
            check(!r.written && !exists("r4_empty.svg") && inbox.take().size() == 1,
                  "perform_save: a size <= 0 is a failure, not a 0-pixel file");
        }

        // ---- A binding max_splits on a 3D SVG: written, inexact, with the warning.
        {
            const FigureSnapshot f3 = inexact_3d_snapshot();
            SvgSaveReport direct;
            export_figure_svg(f3, "r4_direct3d.svg", 420, 360, {.max_splits = 1}, &direct);
            const SaveRequest req{ .path = "r4_save3d.svg", .width = 420, .height = 360,
                                   .max_splits = 1 };
            const SaveResult r = perform_save(gl, nvg, data, f3, req, nullptr, 1, 1.0f);
            check(!direct.scene_order_exact,
                  "perform_save: (control) max_splits = 1 binds on this scene");
            check(r.written && !r.exact && r.warning == direct.warning,
                  "perform_save: a binding bound is written but inexact, with the exporter's warning");
            check(read_all("r4_save3d.svg") == read_all("r4_direct3d.svg"),
                  "perform_save: and the same bytes as export_figure_svg()");
            check(inbox.take().empty(),
                  "perform_save: an inexact save is not a failure, so no \"Save failed\"");

            const SaveRequest roomy{ .path = "r4_save3d.svg", .width = 420, .height = 360 };
            const SaveResult ok = perform_save(gl, nvg, data, f3, roomy, nullptr, 1, 1.0f);
            check(ok.written && ok.exact, "perform_save: with the automatic bound it is exact");
        }

        for (const char* p : {"r4_save.svg", "r4_direct.svg", "r4_save.png", "r4_direct.png",
                              "r4_save3d.svg", "r4_direct3d.svg"})
            remove_file(p);
    }

    // -------------------------------------------------------------------------
    // The dialogs drawn in a window, with no backend
    // -------------------------------------------------------------------------
    namespace {
        // The dialogs as the shell draws them: in their own window, under the
        // figure's id, kept open (the scanner draws every frame).
        void draw_save_window(const FigureSnapshot& fs, FigureEditBox& box, PanelState& st) {
            FigureContext ctx = panel_ctx(fs, box, st);
            st.save.open = true;
            ImGui::Begin("Save Figure");
            push_figure_id(ctx.figure_id);
            (void)draw_save_dialog(ctx, st.save);
            ImGui::PopID();
            ImGui::End();
        }
        void draw_resize_window(const FigureSnapshot& fs, FigureEditBox& box, PanelState& st) {
            FigureContext ctx = panel_ctx(fs, box, st);
            st.resize.open = true;
            st.resize.frame_w = 300;
            st.resize.frame_h = 200;
            ImGui::Begin("Resize");
            push_figure_id(ctx.figure_id);
            (void)draw_resize_dialog(ctx, st.resize);
            ImGui::PopID();
            ImGui::End();
        }
        void draw_warning_window(const FigureSnapshot&, FigureEditBox&, PanelState& st) {
            st.save.warning = "scene order: subplot 1 gave up";
            ImGui::Begin("Export warning###save_warning");
            push_figure_id(st.figure_id);
            draw_save_warning(st.save);
            ImGui::PopID();
            ImGui::End();
        }
    } // namespace

    void test_dialogs_drawn() {
        std::printf("\n[dialogs: drawn in a host's window]\n");

        // A 3D slot too, so the Save dialog shows its export-bound fields.
        FigureSnapshot fs = two_slot_snapshot();
        {
            RenderSnapshot3D rs;
            fs.axes.push_back({AxesSlot{2, 2, 4}, std::move(rs)});
        }

        // ---- Nothing is returned unclicked, and the dialogs stay open.
        {
            ImGuiContext* ictx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ictx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(1280.0f, 900.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            set_live(st.plot, 640, 480);
            FigureEditBox box;
            FigureContext ctx = panel_ctx(fs, box, st);
            open_save_dialog(st.save, ctx);
            open_resize_dialog(st.resize, ctx);
            std::snprintf(st.save.path_buf, sizeof(st.save.path_buf), "%s", "plot.svg");

            int returned = 0;
            std::size_t vertices = 0;
            for (int f = 0; f < 4; ++f) {
                // The cursor sweeps over both windows; no button goes down.
                io.MousePos = ImVec2(40.0f + 60.0f * f, 60.0f + 40.0f * f);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::Begin("Save Figure", &st.save.open);
                push_figure_id(ctx.figure_id);
                if (draw_save_dialog(ctx, st.save)) ++returned;
                ImGui::PopID();
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(400.0f, 0.0f));
                ImGui::Begin("Resize", &st.resize.open);
                push_figure_id(ctx.figure_id);
                if (draw_resize_dialog(ctx, st.resize)) ++returned;
                ImGui::PopID();
                ImGui::End();
                ImGui::Render();
                vertices = 0;
                const ImDrawData* dd = ImGui::GetDrawData();
                for (int n = 0; n < dd->CmdListsCount; ++n)
                    vertices += static_cast<std::size_t>(dd->CmdLists[n]->VtxBuffer.Size);
            }
            check(vertices > 0, "dialogs: both draw into the host's windows");
            check(returned == 0, "dialogs: nothing is returned while nothing is clicked");
            check(st.save.open && st.resize.open, "dialogs: and both stay open");
            check(ImGui::FindWindowByName("Save Figure") && ImGui::FindWindowByName("Resize"),
                  "dialogs: under the host's window names");

            // Closed, a dialog draws nothing and returns nothing.
            st.save.open = false;
            ImGui::NewFrame();
            ImGui::Begin("Host");
            const auto none = draw_save_dialog(ctx, st.save);
            ImGui::End();
            ImGui::Render();
            check(!none, "dialogs: a closed dialog returns nothing");

            ImGui::DestroyContext(ictx);
            ImGui::SetCurrentContext(nullptr);
        }

        // ---- No duplicate widget ids in any of the three.
        for (auto [draw, name] : {
                 std::pair{&draw_save_window, "Save Figure"},
                 std::pair{&draw_resize_window, "Resize"},
                 std::pair{&draw_warning_window, "Export warning###save_warning"}}) {
            const IdConflictScan s = scan_panel_for_id_conflicts(fs, draw, name, {});
            check(s.probes > 0 && s.conflicts == 0,
                  std::string("dialogs: no duplicate widget ids in \"") + name + "\"");
            if (s.conflicts)
                std::printf("    %d of %d cursor positions reported a conflict, first id %u\n",
                            s.conflicts, s.probes, static_cast<unsigned>(s.first));
        }
    }
} // namespace lt
