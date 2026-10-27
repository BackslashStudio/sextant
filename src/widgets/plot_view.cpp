#include "plot_view.h"
#include "panel.h"
#include "figure_context.h"
#include "../figure_edits.h"
#include "../edit_box.h"
#include "../polar_geometry.h"
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
    data.polar().set_nav_active(polar_drag_);
    render_frame(dev, data, fsnap, render_w, render_h, pixel_ratio, nullptr, &fl);
    data.polar().set_nav_active(false);
    plot_fbo.unbind();

    // What only a drawn frame knows, for the other components: the live size,
    // the measurements and the resolved auto limits.
    PlotViewInfo info;
    info.plot_w  = render_w;
    info.plot_h  = render_h;
    info.measure = pv.layout.load();
    info.resolved.reserve(layout.size());
    for (const AxesLayout& al : layout) {
        DrawnLimits r;
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
        // A twin's under its edit key (axes_key()).
        for (const AxesLayout::Twin& t : al.twins) {
            DrawnLimits tw;
            tw.slot = axes_key(al.slot.index, t.layer);
            tw.xmin = t.tr.xmin; tw.xmax = t.tr.xmax;
            tw.ymin = t.tr.ymin; tw.ymax = t.tr.ymax;
            info.resolved.push_back(tw);
        }
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
        in.shift          = io.KeyShift;
        in.escape         = ImGui::IsKeyPressed(ImGuiKey_Escape, false);

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
                                         pv.navigate_enabled && (gate.drag || gate.box || gate.box_done),
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

    // A box zoom being dragged: what it will show, over the image (never saved),
    // clipped to the frame; a side too short to zoom spans the frame.
    if (gate.box) {
        for (const AxesLayout& al : layout) {
            if (al.slot.index != fig.selection.slot) continue;
            const FigureAxesSnapshot* fa = nullptr;
            for (const auto& a : fsnap.axes)
                if (a.slot.index == al.slot.index) { fa = &a; break; }
            if (fa && fa->snappolar()) break;
            const RenderSnapshot* s = fa ? fa->snap2d() : nullptr;
            const CoordTransform& tr = al.tr;
            const auto b = box_pixels(tr, pv.box.x0, pv.box.y0, pv.box.x1, pv.box.y1,
                                      s && s->aspect.ratio > 0.0);
            if (!b) break;
            const float x0 = b->zoom_x ? b->x0 : tr.px, x1 = b->zoom_x ? b->x1 : tr.px + tr.pw;
            const float y0 = b->zoom_y ? b->y0 : tr.py, y1 = b->zoom_y ? b->y1 : tr.py + tr.ph;
            const ImVec2 p0(image_pos.x + x0 / to_plot, image_pos.y + y0 / to_plot);
            const ImVec2 p1(image_pos.x + x1 / to_plot, image_pos.y + y1 / to_plot);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(ImVec2(image_pos.x + tr.px / to_plot, image_pos.y + tr.py / to_plot),
                             ImVec2(image_pos.x + (tr.px + tr.pw) / to_plot,
                                    image_pos.y + (tr.py + tr.ph) / to_plot), true);
            ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            ImVec4 fill = accent;
            fill.w *= 0.15f;
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(fill));
            dl->AddRect(p0, p1, ImGui::GetColorU32(accent), 0.0f, 0, 1.0f);
            dl->PopClipRect();
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
            if (fa && fa->snap2d() && !cell->twins.empty()) {
                // A twinned cell: the nearest across its layers, in that
                // object's own axes' values.
                const CellLayers l = cell_layers(*fa, *cell);
                std::size_t k = 0;
                if (const auto hit = find_pick_layers(l, cursor_x, cursor_y, k, &pv.hint_index,
                                                      fsnap.data_generation, fsnap.pick_radius))
                    hint = HintResult{format_pick(*l.snaps[k], *hit), hit->anchor_x, hit->anchor_y};
            } else if (const RenderSnapshotPolar* sp = fa ? fa->snappolar() : nullptr) {
                hint = find_hint(*sp, cell->tr, cursor_x, cursor_y, fsnap.pick_radius);
            } else if (fa && fa->snap2d()) {
                pv.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                hint = find_hint(*fa->snap2d(), cell->tr, cursor_x, cursor_y,
                                 &pv.hint_index, fsnap.pick_radius);
            } else if (fa && fa->snap3d() && cell->proj3d) {
                pv.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                hint = find_hint3d(*fa->snap3d(), *cell->proj3d, cursor_x, cursor_y,
                                   &pv.hint_index, fsnap.pick_radius);
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

    // Home follows the program's limits every frame, Navigate on or off; the
    // input below tells the history when a gesture starts.
    nav_track_home(pv.nav, fsnap);
    NavInput nav_in;
    nav_in.slot = fig.selection.slot;
    nav_in.time = ImGui::GetTime();
    polar_drag_ = false;

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
            nav_in.drag = gate.drag;

            if (gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                cam = orbit_camera(cam, io.MouseDelta.x * to_plot,
                                        io.MouseDelta.y * to_plot);
                moved = true;
                nav_in.moved = true;
            }

            if (gate.wheel && io.MouseWheel != 0.0f) {
                cam = zoom_camera(cam, io.MouseWheel);
                moved = true;
                nav_in.wheel = true;
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
                    nav_in.fly = true;
                }
            }

            if (moved) {
                sv.camera_local = cam;
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    e.camera = cam;
                    e.camera_seen = sel3d->camera_stamp;
                });
            }
        } else if (const RenderSnapshotPolar* selp = cur ? fsnap.findpolar(cur->slot.index) : nullptr) {
            // A polar cell (PolarAxes): a drag turns the frame about its
            // centre (θ offset), Shift-drag moves r (rmin and rmax together),
            // the wheel scales rmax about rmin. Box zoom has no meaning here.
            const ImGuiIO& io = ImGui::GetIO();
            const PolarMap m = polar_map(*selp);
            const CoordTransform& tr = cur->tr;
            const float cx = tr.to_px(0.0), cy = tr.to_py(0.0);
            const float radius = tr.to_px(1.0) - cx;
            const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
            const auto set_r = [&](double lo, double hi) {
                if (!(hi - lo > 0.0) || !std::isfinite(lo) || !std::isfinite(hi)) return;
                // Onto a turn pushed this frame, if any (one PolarView holds both).
                edit_box.update(cur->slot.index, [&](AxesEdit& e) {
                    PolarView v = e.polar_view.value_or(selp->view);
                    v.rauto = false;
                    v.rmin = lo;
                    v.rmax = hi;
                    e.polar_view = v;
                });
            };
            nav_in.drag = gate.drag || gate.box;
            nav_in.moved = nav_in.drag && moved;
            nav_in.wheel = gate.wheel && io.MouseWheel != 0.0f;
            polar_drag_ = nav_in.drag;
            if (gate.drag && moved && radius > 0.0f) {
                const float x1 = (io.MousePos.x - image_pos.x) * to_plot;
                const float y1 = (io.MousePos.y - image_pos.y) * to_plot;
                const float x0 = x1 - io.MouseDelta.x * to_plot, y0 = y1 - io.MouseDelta.y * to_plot;
                // Screen angles, counterclockwise (y up).
                const double a0 = std::atan2(cy - y0, x0 - cx), a1 = std::atan2(cy - y1, x1 - cx);
                double da = (a1 - a0) / kPolarDeg;
                if (da > 180.0) da -= 360.0;
                if (da < -180.0) da += 360.0;
                edit_box.update(cur->slot.index, [&](AxesEdit& e) {
                    e.polar_view = polar_pan(e.polar_view.value_or(selp->view), da);
                });
            }
            if (gate.box && moved && radius > 0.0f) {
                // Dragging outward pulls the data out: r under the cursor follows it.
                const float dx = io.MouseDelta.x * to_plot, dy = io.MouseDelta.y * to_plot;
                const float x1 = (io.MousePos.x - image_pos.x) * to_plot;
                const float y1 = (io.MousePos.y - image_pos.y) * to_plot;
                const float ux = x1 - cx, uy = y1 - cy, len = std::hypot(ux, uy);
                const float out = len > 0.0f ? (dx * ux + dy * uy) / len : -dy;
                const double dr = -static_cast<double>(out) / radius * (m.rmax - m.rmin);
                set_r(m.rmin + dr, m.rmax + dr);
            }
            if (nav_in.wheel)
                set_r(m.rmin, m.rmin + (m.rmax - m.rmin) * std::pow(0.9, io.MouseWheel));
        } else if (cur && sel2d) {
            const ImGuiIO& io = ImGui::GetIO();

            // Every layer of the cell (Axes::twinx()) moves as if the same input
            // were applied to each, through its own transform; the cell's own
            // axes also updates the slot view. A shared axis moves once through
            // its group whichever layer wrote it.
            struct Layer {
                int key;
                const CoordTransform* tr;
                LimitStamps seen;
            };
            std::vector<Layer> layers{{cur->slot.index, &cur->tr, sel2d->limit_stamps}};
            for (const AxesLayout::Twin& t : cur->twins) {
                const int key = axes_key(cur->slot.index, t.layer);
                if (const RenderSnapshot* ts = fsnap.find2d(key))
                    layers.push_back({key, &t.tr, ts->limit_stamps});
            }
            const auto set_limits = [&](const Layer& l, const AxisLimits& lim) {
                if (l.key == fig.selection.key()) {
                    sv.xmin_local = lim.xmin; sv.xmax_local = lim.xmax;
                    sv.ymin_local = lim.ymin; sv.ymax_local = lim.ymax;
                    sv.xauto_local = sv.yauto_local = false;
                }
                edit_box.update(l.key, [&](AxesEdit& e) {
                    e.xmin = lim.xmin; e.xmax = lim.xmax; e.xlim_auto = false;
                    e.ymin = lim.ymin; e.ymax = lim.ymax; e.ylim_auto = false;
                    e.lim_seen = l.seen;
                });
            };

            nav_in.drag = gate.drag;
            nav_in.moved = gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
            nav_in.wheel = gate.wheel && io.MouseWheel != 0.0f;
            if (gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
                for (const Layer& l : layers)
                    set_limits(l, pan_limits(*l.tr, io.MouseDelta.x * to_plot, io.MouseDelta.y * to_plot));

            if (gate.wheel && io.MouseWheel != 0.0f) {
                const float cursor_x = (io.MousePos.x - image_pos.x) * to_plot;
                const float cursor_y = (io.MousePos.y - image_pos.y) * to_plot;
                const float factor = std::pow(0.9f, io.MouseWheel);
                for (const Layer& l : layers)
                    set_limits(l, zoom_limits(*l.tr, cursor_x, cursor_y, factor));
            }

            // The layers share the frame; each maps the box through its own
            // transform. A ratio (the host's) keeps the frame's shape.
            if (gate.box_done)
                for (const Layer& l : layers)
                    if (const auto b = box_pixels(*l.tr, pv.box.x0, pv.box.y0, pv.box.x1,
                                                  pv.box.y1, sel2d->aspect.ratio > 0.0)) {
                        set_limits(l, box_limits(*l.tr, *b));
                        nav_in.box_done = true;
                    }
        }

        // The view before this frame's edits, if they start a gesture. The
        // double-click is Home for its cell, and records itself.
        if (nav_gesture_starts(pv.nav, nav_in)) nav_begin_gesture(pv.nav, fsnap);
        if (gate.reset && cur) nav_home(pv.nav, fig, cur->slot.index);
    } else {
        nav_gesture_starts(pv.nav, NavInput{});
    }

    // Box-filter the supersampled target into the texture Image() references
    // (sampled later, at RenderDrawData()).
    plot_fbo.resolve();
    return info;
}


} // namespace sextant
