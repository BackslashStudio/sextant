#include "plot_view.h"
#include "panel.h"
#include "figure_context.h"
#include "../figure_edits.h"
#include "../edit_box.h"
#include "../plot_objects.h"
#include "../render_frame.h"
#include "../hint.h"
#include "../event_channel.h"
#include "../plot_events.h"
#include "../window_link.h"
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "../renderer/render_device.h"
#include "../renderer/nvg_renderer.h"
#include "../renderer/figure_layout.h"
#include <imgui.h>
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace sextant {

namespace {

// Hands Figure::connect()'s callbacks this frame's input over the plot: the
// pointer (from the image item's own hover state, so a panel, menu or dialog on
// top never reports), the wheel, a size change, and the keys ImGui did not take
// for a text field (`keys`, from the host; null = none).
void report_plot_events(PlotViewState& pv, const FigureSnapshot& fsnap,
                        const std::vector<AxesLayout>& layout, bool hovered,
                        float x, float y, int plot_w, int plot_h,
                        const PlotEventInfo& what,
                        const std::vector<WindowEvent>* keys) {
    const ImGuiIO& io = ImGui::GetIO();
    EventChannel* ch = pv.events;
    if (!ch) return;

    const std::uint32_t wanted = ch->wanted_mask();
    // ImGui files physical Ctrl under Super and Cmd under Ctrl with its macOS
    // behaviours on; the event reports the physical key.
    const bool ctrl  = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    const bool super = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper;
    const int mods = (ctrl ? kModCtrl : 0) | (io.KeyShift ? kModShift : 0)
                     | (io.KeyAlt ? kModAlt : 0) | (super ? kModSuper : 0);

    PlotInputFrame in;
    in.x = x;
    in.y = y;
    in.hovered = hovered;
    for (int b = 0; b < 3; ++b) {
        in.down[b] = io.MouseDown[b];
        in.double_click[b] = io.MouseDoubleClicked[b];
    }
    in.wheel_x = io.MouseWheelH;
    in.wheel_y = io.MouseWheel;
    in.mods = mods;
    in.width = plot_w;
    in.height = plot_h;

    std::vector<Event> events;
    collect_plot_events(pv.event_tracker, in, what, fsnap, layout, wanted, events,
                        &pv.hint_index);

    if (keys && !io.WantTextInput) {
        for (const WindowEvent& k : *keys) {
            const EventKind kind = k.down ? EventKind::KeyDown : EventKind::KeyUp;
            Event e;
            if ((wanted & event_bit(kind)) && make_key_event(k.key, k.mods, k.down, e))
                events.push_back(std::move(e));
        }
    }
    for (Event& e : events) ch->push(std::move(e));
}

} // namespace


// Renders the plot into the view's target at the given size and shows it via
// ImGui::Image(); the host's window decides where (a resizable dock panel, in
// the library's own window).
PlotViewInfo PlotView::draw(FigureContext& fig, RenderDevice& dev, PlotViewState& pv,
                            const PlotViewParams& params) {
    DataRenderer& data = data_;
    PlotFbo& plot_fbo = fbo_;
    const FigureSnapshot& fsnap = fig.snap;
    FigureEditBox& edit_box = fig.edits;

    // Three units meet here. ImGui's are window coordinates (points on macOS);
    // the FBO is framebuffer pixels (ImGui units x DisplayFramebufferScale);
    // the plot is laid out in logical pixels (framebuffer pixels / the
    // display's content scale), so a font size looks the same on every display
    // and only the sharpness changes. `to_plot` takes ImGui units to plot ones.
    const ImVec2 avail = params.size;
    const float fb_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const float display_scale = std::max(params.display_scale, 0.01f);
    const float to_plot = fb_scale / display_scale;
    const int fb_w = std::max(1, static_cast<int>(avail.x * fb_scale));
    const int fb_h = std::max(1, static_cast<int>(avail.y * fb_scale));
    const int render_w = std::max(1, static_cast<int>(std::lround(fb_w / display_scale)));
    const int render_h = std::max(1, static_cast<int>(std::lround(fb_h / display_scale)));
    pv.live_plot_w.store(render_w, std::memory_order_relaxed);
    pv.live_plot_h.store(render_h, std::memory_order_relaxed);
    pv.live_plot_fb_w.store(fb_w, std::memory_order_relaxed);
    pv.live_plot_fb_h.store(fb_h, std::memory_order_relaxed);
    // The FBO is framebuffer-sized, times the supersample factor; the layout
    // stays logical.
    plot_fbo.ensure_size(fb_w, fb_h, params.supersample);
    const float pixel_ratio = display_scale * static_cast<float>(plot_fbo.supersample());

    // From stored measurements, re-measured on layout generation, size change
    // or File > Refit layout.
    const FigureLayout fl = pv.layout.fit(fsnap, render_w, render_h);

    // What is drawn, for hit-testing: the cells render_frame() lays out.
    const std::vector<AxesLayout> layout = axes_layouts(fl);
    plot_fbo.bind();
    render_frame(dev, data, fsnap, render_w, render_h, pixel_ratio, nullptr, &fl);
    plot_fbo.unbind();

    // What only a drawn frame knows, for the other components: the live size,
    // the measurements and the resolved auto limits.
    PlotViewInfo info;
    info.plot_w  = render_w;
    info.plot_h  = render_h;
    info.measure = pv.layout.load();
    info.resolved.reserve(layout.size());
    for (const AxesLayout& al : layout) {
        ResolvedLimits r;
        r.slot = al.slot.index;
        if (al.proj3d) {
            const Transform3D& t = al.proj3d->transform();
            r.is_3d = true;
            r.xmin = t.xmin; r.xmax = t.xmax;
            r.ymin = t.ymin; r.ymax = t.ymax;
            r.zmin = t.zmin; r.zmax = t.zmax;
        } else {
            r.xmin = al.tr.xmin; r.xmax = al.tr.xmax;
            r.ymin = al.tr.ymin; r.ymax = al.tr.ymax;
        }
        info.resolved.push_back(r);
    }

    // GL textures are bottom-up; flip v.
    const ImVec2 image_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(plot_fbo.color_texture()), avail,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

    // An invisible button over the image provides hover/active tracking,
    // keeping a drag active if the cursor leaves the image.
    ImGui::SetCursorScreenPos(image_pos);
    ImGui::InvisibleButton("##plot_nav", avail);

    // Selection and navigation gating, read while the button is the last item.
    PlotNavGate gate;
    const bool in_hovered = ImGui::IsItemHovered();
    {
        const ImGuiIO& io = ImGui::GetIO();
        PlotPointer in;
        in.x = (io.MousePos.x - image_pos.x) * to_plot;
        in.y = (io.MousePos.y - image_pos.y) * to_plot;
        in.hovered        = in_hovered;
        in.active         = ImGui::IsItemActive();
        in.pressed        = ImGui::IsItemActivated();
        in.double_clicked = in.pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        in.released       = ImGui::IsItemDeactivated();
        in.dragged        = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left]
                            >= io.MouseDragThreshold * io.MouseDragThreshold;

        // Grid boundary dragging first; what it owns doesn't select or
        // navigate.
        GridDragOut grid = update_grid_drag(pv, fsnap, fl, render_w, render_h, in,
                                            4.0f * to_plot);
        if (grid.col_ratios || grid.row_ratios)
            edit_box.update_figure([&](FigureEdits& f) {
                if (grid.col_ratios) f.col_ratios = std::move(*grid.col_ratios);
                if (grid.row_ratios) f.row_ratios = std::move(*grid.row_ratios);
            });
        if (grid.cursor_ew) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (grid.cursor_ns) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (grid.owns) {
            in.hovered = in.active = in.pressed = in.double_clicked = in.released = false;
        }
        const int selected_before = fig.selection.slot;
        gate = update_plot_selection(fig.selection, pv, fsnap, layout, in);

        // Report what happened over the plot, now that it is known what the
        // panel did with it. Read-only: nothing below changes the input.
        report_plot_events(pv, fsnap, layout, in_hovered, in.x, in.y, render_w, render_h,
                           PlotEventInfo{grid.owns,
                                         pv.navigate_enabled && gate.drag,
                                         pv.navigate_enabled && gate.wheel,
                                         pv.navigate_enabled && gate.reset,
                                         fig.selection.slot != selected_before},
                           params.keys);
    }

    // Outline the selected subplot, drawn over the image (never saved). Only
    // with more than one subplot.
    if (fsnap.axes.size() > 1) {
        for (const AxesLayout& al : layout) {
            if (al.slot.index != fig.selection.slot) continue;
            const ImVec2 p0(image_pos.x + al.cell.x / to_plot + 1.0f,
                            image_pos.y + al.cell.y / to_plot + 1.0f);
            const ImVec2 p1(image_pos.x + (al.cell.x + al.cell.w) / to_plot - 1.0f,
                            image_pos.y + (al.cell.y + al.cell.h) / to_plot - 1.0f);
            ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            accent.w *= 0.75f;
            ImGui::GetWindowDrawList()->AddRect(p0, p1, ImGui::GetColorU32(accent),
                                                0.0f, 0, 1.5f);
            break;
        }
    }

    // Hover hints over any subplot (independent of selection), drawn into the
    // already-rendered texture in a new NanoVG frame.
    if (pv.hints_enabled && in_hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        const float cursor_x = (io.MousePos.x - image_pos.x) * to_plot;
        const float cursor_y = (io.MousePos.y - image_pos.y) * to_plot;
        if (const AxesLayout* cell = find_hint_cell(layout, cursor_x, cursor_y)) {
            const FigureAxesSnapshot* fa = nullptr;
            for (const auto& a : fsnap.axes)
                if (a.slot.index == cell->slot.index) { fa = &a; break; }
            // 3D: ray cast against planes and bars, then the 2D search on the
            // nearest plane hit.
            std::optional<HintResult> hint;
            if (fa && fa->snap2d()) {
                pv.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                hint = find_hint(*fa->snap2d(), cell->tr, cursor_x, cursor_y,
                                 &pv.hint_index);
            } else if (fa && fa->snap3d() && cell->proj3d) {
                pv.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                hint = find_hint3d(*fa->snap3d(), *cell->proj3d, cursor_x, cursor_y,
                                   &pv.hint_index);
            }
            if (hint) {
                plot_fbo.bind();
                glViewport(0, 0, plot_fbo.render_width(), plot_fbo.render_height());
                dev.begin_nvg_frame(render_w, render_h, pixel_ratio);
                dev.renderer().draw_hint(render_w, render_h, hint->anchor_x, hint->anchor_y,
                                         hint->text);
                dev.end_nvg_frame();
                plot_fbo.unbind();
            }
        }
    }

    if (pv.navigate_enabled) {
        // Pulled here, after any selection change above: a drag starts from the
        // selected slot's own camera and limits, whichever panels are drawn.
        SlotViewState& sv = slot_view(fig);
        const AxesLayout* cur = nullptr;
        for (const auto& al : layout)
            if (al.slot.index == fig.selection.slot) { cur = &al; break; }

        // A 3D slot navigates its camera, a 2D slot its limits. Both push
        // through FigureEditBox with the stamps they worked over, so the view
        // survives refresh() until the program sets it.
        const RenderSnapshot3D* sel3d = nullptr;
        const RenderSnapshot*   sel2d = nullptr;
        for (const auto& a : fsnap.axes)
            if (a.slot.index == fig.selection.slot) {
                sel3d = a.snap3d(); sel2d = a.snap2d(); break;
            }

        if (cur && sel3d) {
            const ImGuiIO& io = ImGui::GetIO();
            const int idx = cur->slot.index;
            Camera3D cam = sv.camera_local;
            bool moved = false;

            if (gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                cam = orbit_camera(cam, io.MouseDelta.x * to_plot,
                                        io.MouseDelta.y * to_plot);
                moved = true;
            }

            if (gate.wheel && io.MouseWheel != 0.0f) {
                cam = zoom_camera(cam, io.MouseWheel);
                moved = true;
            }

            // Fly keys only while the selected cell is hovered or dragged and
            // ImGui doesn't want the keyboard (typing "W" shouldn't fly).
            if (gate.keys && !io.WantCaptureKeyboard) {
                FlyInput fly;
                fly.forward = ImGui::IsKeyDown(ImGuiKey_W);
                fly.back    = ImGui::IsKeyDown(ImGuiKey_S);
                fly.left    = ImGui::IsKeyDown(ImGuiKey_A);
                fly.right   = ImGui::IsKeyDown(ImGuiKey_D);
                fly.up      = ImGui::IsKeyDown(ImGuiKey_E);
                fly.down    = ImGui::IsKeyDown(ImGuiKey_Q);
                fly.dt      = io.DeltaTime;
                if (fly.forward || fly.back || fly.left || fly.right || fly.up || fly.down) {
                    // The camera basis from this frame's projector (A/D strafe,
                    // W/S dolly along the view).
                    cam = fly_camera(cam,
                                     cur->proj3d ? cur->proj3d->right()
                                                 : Vec3{ 0.0, 1.0, 0.0 },
                                     cur->proj3d ? cur->proj3d->forward()
                                                 : Vec3{ -1.0, 0.0, 0.0 },
                                     fly);
                    moved = true;
                }
            }

            if (gate.reset) {
                cam = sel3d->default_camera;
                moved = true;
            }

            if (moved) {
                sv.camera_local = cam;
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    e.camera = cam;
                    e.camera_seen = sel3d->camera_stamp;
                });
            }
        } else if (cur && sel2d) {
            const ImGuiIO& io = ImGui::GetIO();
            const LimitStamps seen = sel2d->limit_stamps;
            const int idx = cur->slot.index;

            if (gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                const auto lim = pan_limits(cur->tr,
                    io.MouseDelta.x * to_plot, io.MouseDelta.y * to_plot);
                sv.xmin_local = lim.xmin; sv.xmax_local = lim.xmax;
                sv.ymin_local = lim.ymin; sv.ymax_local = lim.ymax;
                sv.xauto_local = sv.yauto_local = false;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xmin = lim.xmin; e.xmax = lim.xmax; e.xlim_auto = false;
                    e.ymin = lim.ymin; e.ymax = lim.ymax; e.ylim_auto = false;
                    e.lim_seen = seen;
                });
            }

            if (gate.wheel && io.MouseWheel != 0.0f) {
                const float cursor_x = (io.MousePos.x - image_pos.x) * to_plot;
                const float cursor_y = (io.MousePos.y - image_pos.y) * to_plot;
                const float factor = std::pow(0.9f, io.MouseWheel);
                const auto lim = zoom_limits(cur->tr, cursor_x, cursor_y, factor);
                sv.xmin_local = lim.xmin; sv.xmax_local = lim.xmax;
                sv.ymin_local = lim.ymin; sv.ymax_local = lim.ymax;
                sv.xauto_local = sv.yauto_local = false;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xmin = lim.xmin; e.xmax = lim.xmax; e.xlim_auto = false;
                    e.ymin = lim.ymin; e.ymax = lim.ymax; e.ylim_auto = false;
                    e.lim_seen = seen;
                });
            }

            if (gate.reset) {
                sv.xauto_local = sv.yauto_local = true;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xlim_auto = true; e.ylim_auto = true;
                    e.lim_seen = seen;
                });
            }
        }
    }

    // Box-filter the supersampled target into the texture Image() references
    // (sampled later, at RenderDrawData()).
    plot_fbo.resolve();
    return info;
}


} // namespace sextant
