// The plot view as a kit component (GUI-kit R6): axes_layouts() against what
// render_frame() hands back, PlotView::draw() in a host's window with no sextant
// backend (what it returns, the keys it is given), two figures' views in one
// window, and the inspectors with no view on screen.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "event_channel.h"
#include "render_frame.h"
#include "renderer/plot_fbo.h"
#include "window_link.h"

namespace lt {
    using namespace sextant;

    namespace {
        // A 2D line (slot 1) beside a 3D surface (slot 2), both on auto limits.
        FigureSnapshot line_and_surface() {
            FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
            fs.axes[0].slot = AxesSlot{1, 2, 1};
            RenderSnapshot3D r;
            r.surfaces.push_back(ripple_surface());
            fs.axes.push_back({AxesSlot{1, 2, 2}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            return fs;
        }

        bool same_layout(const AxesLayout& a, const AxesLayout& b) {
            if (a.slot.index != b.slot.index) return false;
            if (a.cell.x != b.cell.x || a.cell.y != b.cell.y ||
                a.cell.w != b.cell.w || a.cell.h != b.cell.h) return false;
            if (a.tr.xmin != b.tr.xmin || a.tr.xmax != b.tr.xmax ||
                a.tr.ymin != b.tr.ymin || a.tr.ymax != b.tr.ymax) return false;
            if (a.proj3d.has_value() != b.proj3d.has_value()) return false;
            if (a.proj3d) {
                const Transform3D& s = a.proj3d->transform();
                const Transform3D& t = b.proj3d->transform();
                if (s.xmin != t.xmin || s.xmax != t.xmax || s.ymin != t.ymin ||
                    s.ymax != t.ymax || s.zmin != t.zmin || s.zmax != t.zmax) return false;
            }
            return true;
        }

        // A headless ImGui context: no platform or renderer backend.
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

        void end_imgui(ImGuiContext* ictx) {
            ImGui::DestroyContext(ictx);
            ImGui::SetCurrentContext(nullptr);
        }
    } // namespace

    // -------------------------------------------------------------------------
    // axes_layouts(): render_frame()'s out_layout, without GL
    // -------------------------------------------------------------------------
    void test_axes_layouts() {
        std::printf("\n[plot view: axes_layouts()]\n");

        const FigureSnapshot fs = line_and_surface();
        constexpr int W = 520, H = 300;
        const std::vector<AxesLayout> pure = axes_layouts(compute_figure_layout(fs, W, H));

        GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});
        RenderDevice dev;
        DataRenderer data;
        PlotFbo fbo;
        fbo.ensure_size(W, H);
        fbo.bind();
        std::vector<AxesLayout> drawn;
        render_frame(dev, data, fs, W, H, 1.0f, &drawn);
        fbo.unbind();

        bool same = pure.size() == drawn.size() && pure.size() == 2;
        for (std::size_t i = 0; same && i < pure.size(); ++i) same = same_layout(pure[i], drawn[i]);
        check(same, "axes_layouts: the same list render_frame() hands back, cell for cell");
        check(pure.size() == 2 && !pure[0].proj3d && pure[1].proj3d,
              "axes_layouts: a projector for the 3D cell only");
    }

    // -------------------------------------------------------------------------
    // PlotView::draw() in a host's window, with no sextant backend
    // -------------------------------------------------------------------------
    void test_plot_view_draw() {
        std::printf("\n[plot view: drawn in a host's window]\n");

        GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});
        RenderDevice dev;
        PlotView view;

        const FigureSnapshot fs = line_and_surface();
        FigureEditBox box;
        PanelState st;
        auto ch = EventChannel::create();
        st.plot.events = ch.get();
        std::vector<std::string> keys_seen;
        ch->connect(EventKind::KeyDown, [&](const Event& e) { keys_seen.push_back(e.key); });

        ImGuiContext* ictx = bare_imgui(900.0f, 700.0f);
        // What the view's last draw() returned, as a shell keeps it.
        FigureContext ctx = panel_ctx(fs, box, st);

        const std::vector<WindowEvent> key_a{
            WindowEvent{ .kind = WindowEvent::Kind::Key, .key = 65, .down = true } };
        auto frame = [&](const std::vector<WindowEvent>* keys) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
            ImGui::Begin("Host");
            push_figure_id(ctx.figure_id);
            st.plot_info = view.draw(ctx, dev, st.plot,
                                     PlotViewParams{ .size = ImVec2(420.0f, 300.0f),
                                                     .display_scale = 1.0f,
                                                     .supersample = 1,
                                                     .keys = keys });
            ImGui::PopID();
            ImGui::End();
            ImGui::Render();
        };

        // ---- What one frame returns.
        frame(nullptr);
        const PlotViewInfo& info = st.plot_info;
        check(info.plot_w == 420 && info.plot_h == 300,
              "plot view: returns the size it laid out, in logical pixels");
        check(st.plot.live_plot_w.load() == 420 && st.plot.live_plot_h.load() == 300,
              "plot view: and publishes it to its state for other threads");
        check(info.measure && info.measure == st.plot.layout.load(),
              "plot view: returns the measurements it laid out with");

        const std::vector<AxesLayout> want =
            axes_layouts(compute_figure_layout(fs, *info.measure, 420, 300));
        bool limits = info.resolved.size() == want.size() && want.size() == 2;
        for (const AxesLayout& al : want) {
            const ResolvedLimits* r = info.resolved_for(al.slot.index);
            if (!r) { limits = false; break; }
            if (al.proj3d) {
                const Transform3D& t = al.proj3d->transform();
                limits = limits && r->is_3d && r->xmin == t.xmin && r->xmax == t.xmax &&
                         r->ymin == t.ymin && r->ymax == t.ymax &&
                         r->zmin == t.zmin && r->zmax == t.zmax;
            } else {
                limits = limits && !r->is_3d && r->xmin == al.tr.xmin && r->xmax == al.tr.xmax &&
                         r->ymin == al.tr.ymin && r->ymax == al.tr.ymax;
            }
        }
        check(limits, "plot view: returns every slot's resolved limits, 2D and 3D");
        // A copy: the frames below replace st.plot_info.
        const std::optional<ResolvedLimits> r2 =
            info.resolved_for(1) ? std::optional(*info.resolved_for(1)) : std::nullopt;
        check(r2 && r2->xmin <= 0.0 && r2->xmax >= 2.0 && r2->ymin <= 0.0 && r2->ymax >= 4.0,
              "plot view: (the 2D slot's auto limits cover its data)");

        bool image = false;
        if (const ImGuiWindow* w = ImGui::FindWindowByName("Host"))
            for (const ImDrawCmd& cmd : w->DrawList->CmdBuffer)
                if (cmd.TexRef.GetTexID() == static_cast<ImTextureID>(view.fbo().color_texture()))
                    image = true;
        check(image && view.fbo().width() == 420 && view.fbo().height() == 300,
              "plot view: shows its own target, sized to what it was given");

        // ---- Keys: only the ones the host passes.
        frame(nullptr);
        ch->dispatch();
        check(keys_seen.empty(), "plot view: no keys given, no key events");
        frame(&key_a);
        ch->dispatch();
        check(keys_seen.size() == 1 && keys_seen[0] == "a",
              "plot view: a key the host passes becomes a key event");

        // ---- The inspectors read the info: with none, the declared limits.
        {
            PanelState blind;
            FigureContext none{ fs, box, blind.selection, blind.slot_view, nullptr, 7 };
            ImGui::NewFrame();
            ImGui::Begin("Cosmetic");
            draw_cosmetic_panel(none, blind.cosmetic);
            ImGui::End();
            ImGui::Render();
            const RenderSnapshot& s = *fs.axes[0].snap2d();
            check(blind.slot_view.xmin_local == s.xmin && blind.slot_view.xmax_local == s.xmax &&
                  blind.slot_view.ymin_local == s.ymin && blind.slot_view.ymax_local == s.ymax,
                  "plot view: with no view on screen, Cosmetic shows the declared limits");

            ImGui::NewFrame();
            ImGui::Begin("Cosmetic");
            draw_cosmetic_panel(ctx, st.cosmetic);
            ImGui::End();
            ImGui::Render();
            const SlotViewState& sv = st.slot_view;
            check(r2 && sv.xmin_local == r2->xmin && sv.xmax_local == r2->xmax &&
                  sv.ymin_local == r2->ymin && sv.ymax_local == r2->ymax,
                  "plot view: (control) with one, the limits it resolved");
        }

        end_imgui(ictx);
    }

    // -------------------------------------------------------------------------
    // Two figures' views in one window
    // -------------------------------------------------------------------------
    void test_plot_view_two_figures() {
        std::printf("\n[plot view: two figures in one window]\n");

        GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});
        RenderDevice dev;
        PlotView view_a, view_b;

        const FigureSnapshot fa = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        const FigureSnapshot fb = line_and_surface();
        FigureEditBox box_a, box_b;
        PanelState st_a, st_b;

        // The number of hover positions that reported a duplicate id, with the
        // two views under `id_a` and `id_b`.
        auto scan = [&](std::uint64_t id_a, std::uint64_t id_b) {
            ImGuiContext* ictx = bare_imgui(600.0f, 800.0f);
            ImGui::GetIO().ConfigDebugHighlightIdConflicts = true;
            FigureContext ca{ fa, box_a, st_a.selection, st_a.slot_view, &st_a.plot_info, id_a };
            FigureContext cb{ fb, box_b, st_b.selection, st_b.slot_view, &st_b.plot_info, id_b };
            ImVec2 mid_a, mid_b;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(520.0f, 760.0f));
                ImGui::Begin("Host");
                const PlotViewParams p{ .size = ImVec2(400.0f, 300.0f) };
                push_figure_id(ca.figure_id);
                st_a.plot_info = view_a.draw(ca, dev, st_a.plot, p);
                mid_a = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                               (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
                ImGui::PopID();
                push_figure_id(cb.figure_id);
                st_b.plot_info = view_b.draw(cb, dev, st_b.plot, p);
                mid_b = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                               (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
                ImGui::PopID();
                ImGui::End();
                ImGui::Render();
            };
            frame();
            int conflicts = 0;
            for (int k = 0; k < 2; ++k) {
                ImGui::GetIO().MousePos = k == 0 ? mid_a : mid_b;
                frame();
                frame();
                if (ImGui::GetCurrentContext()->DebugDrawIdConflictsId != 0) ++conflicts;
            }
            end_imgui(ictx);
            return conflicts;
        };

        check(scan(1, 2) == 0, "two views: each figure's view keeps its own widget ids");
        check(scan(5, 5) > 0, "two views: (control) under one id they would collide");
        check(st_a.plot_info.resolved.size() == 1 && st_b.plot_info.resolved.size() == 2,
              "two views: each returns its own figure's slots");
    }
} // namespace lt
